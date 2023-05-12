import collections
import sys
import os.path
import json
import fnmatch
import util
import subprocess
import platform
import shutil
import time
import dependencies
import glob
import jsn.jsn as jsn
import cgu.cgu as cgu
from http.server import HTTPServer, CGIHTTPRequestHandler
import webbrowser
import threading


# returns tool to run from cmdline with .exe
def tool_to_platform(tool):
    tool = util.sanitize_file_path(tool)
    tool = tool.replace("$platform", util.get_platform_name())
    if platform.system() == "Windows":
        tool += ".exe"
    return tool


# ensure running with python3 or py -3
def python_tool_to_platform(tool):
    tool = util.sanitize_file_path(tool)
    tool = f"py -3 {tool}" if platform.system() == "Windows" else f"python3 {tool}"
    return tool


# single model build / optimise ran on a separate thread
def run_models_thread(cmd):
    p = subprocess.Popen(cmd, shell=True)
    p.wait()


# models
def run_models(config, task_name, files):
    tool_cmd = python_tool_to_platform(config["tools"]["build_models"])
    threads = []
    mesh_opt = ""
    if os.path.exists(config["tools"]["mesh_opt"]):
        mesh_opt = config["tools"]["mesh_opt"]
    for f in files:
        cmd = f" -i {f[0]} -o {os.path.dirname(f[1])}"
        if len(mesh_opt) > 0:
            cmd += f" -mesh_opt {mesh_opt}"
        x = threading.Thread(target=run_models_thread, args=(tool_cmd + cmd,))
        threads.append(x)
        x.start()
    for t in threads:
        t.join()



# generates function pointer bindings to call pmtech from a live reloaded dll.
def run_cr(config, task_name):
    print("--------------------------------------------------------------------------------")
    print("cr -----------------------------------------------------------------------------")
    print("--------------------------------------------------------------------------------")
    print(config[task_name]["output"])
    files = config[task_name]["file_list"]
    free_funcs = []
    added = []
    for f in files:
        source = open(f, "r").read()
        source = cgu.remove_comments(source)
        strings, source = cgu.placeholder_string_literals(source)
        functions, function_names = cgu.find_functions(source)
        for func in functions:
            free = next(
                (False for s in func["scope"] if s["type"] == "struct"),
                len(func["qualifier"]) == 0,
            )
            # cant add members
            if not free:
                continue
            # cant add overloads
            if func["name"] in added:
                continue
            func["file"] = os.path.basename(f)
            added.append(func["name"])
            free_funcs.append(func)

    # start writing code
    code = cgu.src_line("// codegen_2")
    code += cgu.src_line("#pragma once")
    for f in files:
        bn = os.path.basename(f)
        code += cgu.src_line(f'#include {cgu.in_quotes(bn)}')

    code += cgu.src_line("using namespace pen;")
    code += cgu.src_line("using namespace put;")
    code += cgu.src_line("using namespace pmfx;")
    code += cgu.src_line("using namespace ecs;")
    code += cgu.src_line("using namespace dbg;")

    # sort by immediate scope
    scope_funcs = {}
    for f in free_funcs:
        ff = f["file"]
        l = len(f["scope"])
        if l > 0:
            s = f["scope"][l-1]["name"]
            if s not in scope_funcs.keys():
                scope_funcs[s] = []
            scope_funcs[s].append(f)

    # add bindings grouped by scope
    for scope, value in scope_funcs.items():
        # function pointer typedefs
        for f in value:
            args = cgu.get_funtion_prototype(f)
            code += cgu.src_line("typedef " + f["return_type"] + " (*proc_" + f["name"] + ")" + args + ";")
        # struct
        struct_name = f"__{scope}"
        code += cgu.src_line(f"struct {struct_name}" + " {")
        code += cgu.src_line(f"void* {struct_name}_start;")
        # function pointers members
        for f in scope_funcs[scope]:
            code += cgu.src_line("proc_" + f["name"] + " " + f["name"] + ";")
        code += cgu.src_line(f"void* {struct_name}_end;")
        code += cgu.src_line("};")

    # pointers to contexts
    inherit = ""
    for scope in scope_funcs:
        if len(inherit) > 0:
            inherit += ", "
        inherit += f"public __{scope}"
    code += cgu.src_line("struct live_context:")
    code += cgu.src_line(inherit + "{")
    code += cgu.src_line("f32 dt;")
    code += cgu.src_line("ecs::ecs_scene* scene;")
    for scope in scope_funcs:
        code += cgu.src_line(f"__{scope}* {scope}_funcs;")
    code += cgu.src_line("live_context() {")
    # bind function pointers to addresses
    code += cgu.src_line("#if !DLL")
    for value_ in scope_funcs.values():
        for f in value_:
            full_scope = "".join(
                q["name"] + "::"
                for q in f["scope"]
                if q["type"] == "namespace"
            )
            code += cgu.src_line(f["name"] + " = &" + full_scope + f["name"] + ";")
    code += cgu.src_line("#endif")
    code += cgu.src_line("}")

    code += cgu.src_line("};")
    output_file = open(config[task_name]["output"], "w")
    output_file.write(cgu.format_source(code, 4))
    return


# entry point of pmbuild_ext
