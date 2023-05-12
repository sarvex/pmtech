# lightweight json format without the need for quotes, allowing comments, file importing, inheritence and more
# copyright Alex Dixon 2019: https://github.com/polymonster/jsn/blob/master/license

import json
import sys
import os
import traceback
import platform


# struct to store the build info for jobs from parsed commandline args
class BuildInfo:
    inputs = []         # list of files
    import_dirs = []    # lst of import directories to search
    output_dir = ""     # output directory
    print_out = False   # print out the resulting json from jsn to the console


# parse command line args passed in
def parse_args():
    info = BuildInfo()
    if len(sys.argv) == 1:
        display_help()
    for i in range(1, len(sys.argv)):
        if sys.argv[i] == "-i":
            j = i + 1
            while j < len(sys.argv) and sys.argv[j][0] != '-':
                info.inputs.append(sys.argv[j])
                j = j + 1
            i = j
        elif sys.argv[i] == "-I":
            j = i + 1
            while j < len(sys.argv) and sys.argv[j][0] != '-':
                info.import_dirs.append(sys.argv[j])
                j = j + 1
            i = j
        elif sys.argv[i] == "-o":
            info.output_dir = sys.argv[i + 1]
        elif sys.argv[i] == "-p":
            info.print_out = True
    return info


# help
def display_help():
    print("commandline arguments:")
    print("    -help display this message")
    print("    -i list of input files or directories to process")
    print("    -o output file or directory ")
    print("    -I list of import directories, to search for imports")
    print("    -p print output to console ")


# do c like (u32)-1
def us(v):
    return sys.maxsize if v == -1 else v


# return string inside "quotes" to make code gen cleaner
def in_quotes(string):
    return string if len(string) >= 2 and string[0] == "\"" else f'"{string}"'


# create a new dir for a file or a folder if it doesnt already exist and not throw an exception
def create_dir(dst_file):
    dir = dst_file
    if is_file(dir):
        dir = os.path.dirname(dir)
    if len(dir) == 0:
        return
    os.makedirs(dir, exist_ok=True)


# change extension
def change_ext(file, ext):
    return os.path.splitext(file)[0] + ext


# is_file
def is_file(file):
    return len(os.path.splitext(file)[1]) > 0


# python json style dumps
def format(jsn, indent=4):
    nl = ["{", "[", ","]
    el = ["}", "]"]
    id = ["{", "["]
    fmt = ""
    cur_indent = 0
    str_list = find_strings(jsn)
    for c in range(0, len(jsn)):
        char = jsn[c]
        if is_inside_quotes(str_list, c):
            fmt += char
            continue
        if char in el:
            fmt += "\n"
            cur_indent -= 4
            for _ in range(0, cur_indent):
                fmt += " "
        fmt += char
        if char in nl:
            fmt += "\n"
            if char in id:
                cur_indent += 4
            for _ in range(0, cur_indent):
                fmt += " "
        if char == ":":
            fmt += " "
    return fmt


# check whether char jsn[pos] is inside quotes or not
def is_inside_quotes(str_list, pos):
    for s in str_list:
        if pos < s[0]:
            break
        if s[0] < pos < s[1]:
            return s[1]+1
    return 0


# find all string tokens within jsn source marked by start and end index
def find_strings(jsn):
    quote_types = ["\"", "'"]
    oq = ""
    prev_char = ""
    istart = -1
    str_list = []
    for ic in range(0, len(jsn)):
        c = jsn[ic]
        if c in quote_types:
            if oq == "":
                oq = c
                istart = ic
            elif oq == c and prev_char != "\\":
                oq = ""
                str_list.append((istart, ic))
        prev_char = "" if prev_char == "\\" and c == "\\" else c
    return str_list


# trims whitespace from lines
def trim_whitespace(jsn):
    lines = jsn.split('\n')
    return "".join(l.strip() + "\n" for l in lines)


# remove whitespace and newlines to simplify subsequent ops
def clean_src(jsn):
    clean = ""
    inside_quotes = False
    for char in jsn:
        if char == '\"':
            inside_quotes = not inside_quotes
        strip_char = char.strip() if not inside_quotes else char
        clean += strip_char
    return clean


# remove comments, taken from https:/github.com/polymonster/stub-format/stub_format.py
def remove_comments(file_data):
    lines = file_data.split("\n")
    inside_block = False
    conditioned = ""
    for line in lines:
        str_list = find_strings(line)
        if inside_block:
            ecpos = line.find("*/")
            if ecpos == -1:
                continue
            inside_block = False
            line = line[ecpos+2:]
        cpos = line.find("//")
        mcpos = line.find("/*")

        if is_inside_quotes(str_list, mcpos):
            mcpos = -1

        if is_inside_quotes(str_list, cpos):
            cpos = -1

        if cpos != -1:
            conditioned += line[:cpos] + "\n"
        elif mcpos != -1:
            conditioned += line[:mcpos] + "\n"
            inside_block = True
        else:
            conditioned += line + "\n"
    return conditioned


# change single quotes to double quotes to support json5
def change_quotes(jsn):
    str_list = find_strings(jsn)
    conditioned = ""
    prev = ""
    for c in range(0, len(jsn)):
        if c > 0:
            prev = jsn[c-1]
        char = jsn[c]
        if char == "'":
            if not is_inside_quotes(str_list, c):
                conditioned += "\""
                continue
        elif char == "\"":
            if is_inside_quotes(str_list, c) and prev != "\\":
                conditioned += "\\\""
                continue
        conditioned += char
    return conditioned


# remove line breaks within strings
def collapse_line_breaks(jsn):
    str_list = find_strings(jsn)
    conditioned = ""
    skip = False
    for c in range(0, len(jsn)):
        char = jsn[c]
        if skip:
            skip = False
            continue
        if (
            char == "\\"
            and c + 1 < len(jsn)
            and jsn[c + 1] == "\n"
            and is_inside_quotes(str_list, c)
        ):
            skip = True
            continue
        conditioned += char
    return conditioned


# find first char in chars in string from pos
def find_first(string, pos, chars):
    first = us(-1)
    for char in chars:
        first = min(us(string.find(char, pos)), first)
    return first


# get value type, object, array, int, float, bool, hex, binary, binary shift
def get_value_type(value):
    value = value.strip()
    if len(value) > 0:
        if value[0] == "\"":
            return "string"
        if value[0] == "{":
            return "object"
        if value[0] == "[":
            return "array"
        if value in ['true', 'false']:
            return "bool"
        if value.find(".") != -1:
            try:
                float(value)
                return "float"
            except ValueError:
                pass
        if value.find("0x") != -1:
            try:
                int(value[2:], 16)
                return "hex"
            except ValueError:
                pass
        if value.find("0b") != -1:
            try:
                int(value[2:], 2)
                return "binary"
            except ValueError:
                pass
        if value.find("<<") != -1 or value.find(">>") != -1:
            return "binary_shift"
        try:
            int(value)
            return "int"
        except ValueError:
            pass
    return "string"


# find inherits inside unquoted objects - key(inherit_a, inherit_b)
def get_inherits(object_key):
    if object_key[0] == "\"":
        return object_key, []
    bp = object_key.find("(")
    if bp != -1:
        ep = object_key.find(")")
        i = object_key[bp+1:ep]
        ii = i.split(",")
        return object_key[:bp], ii
    return object_key, []


# finds the end of a pair of brackets, enclosing sub brackets inside them
def enclose_brackets(open, close, string, pos):
    start = pos
    pos = string.find(open, pos)
    stack = [open]
    pos += 1
    while stack and pos < len(string):
        if string[pos] == open:
            stack.append(open)
        if string[pos] == close:
            stack.pop()
        pos += 1
    return pos


# add quotes and convert values to be json compatible
def quote_value(value, pos, next):
    quoted = ""
    if get_value_type(value) == "string":
        quoted += in_quotes(value)
        pos = next
    elif get_value_type(value) == "hex":
        hex_value = int(value[2:], 16)
        quoted = str(hex_value)
        pos = next
    elif get_value_type(value) == "binary":
        bin_value = int(value[2:], 2)
        quoted = str(bin_value)
        pos = next
    elif get_value_type(value) == "binary_shift":
        components = value.split("|")
        bv = 0
        for comp in components:
            if comp.find("<<") != -1:
                comp = comp.split("<<")
                bv |= int(comp[0]) << int(comp[1])
            elif comp.find(">>") != -1:
                comp = comp.split(">>")
                bv |= int(comp[0]) << int(comp[1])
            else:
                bv |= int(comp)
        quoted = str(bv)
        pos = next
    elif get_value_type(value) == "float":
        f = value
        if f[0] == ".":
            f = f"0{f}"
        elif f[len(f)-1] == ".":
            f = f"{f}0"
        quoted = f
        pos = next
    elif get_value_type(value) == "int":
        i = value
        if i[0] == "+":
            i = i[1:]
        quoted = i
        pos = next
    return (quoted, pos)


# add quotes to array items
def quote_array(jsn):
    if not jsn:
        return f"[{jsn}]"
    # arrays can contain mixed data so go element wise
    pos = 0
    element_wise = ""
    while True:
        elem_end = jsn.find(",", pos)
        if elem_end == -1:
            elem_end = len(jsn)
        elem = jsn[pos:elem_end].strip()
        if len(elem) == 0:
            break
        if get_value_type(elem) == "object":
            elem_end = enclose_brackets("{", "}", jsn, pos)
            sub_object = jsn[pos:elem_end]
            element_wise += quote_object(sub_object)
        elif get_value_type(elem) == "array":
            elem_end = enclose_brackets("[", "]", jsn, pos)
            sub_array = jsn[pos+1:elem_end-1]
            element_wise += quote_array(sub_array)
        elif elem[0] == '\"':
            elem_end += enclose_brackets("\"", "\"", jsn, pos)
            element_wise = jsn[pos:elem_end]
        else:
            element_wise += quote_value(elem, 0, 0)[0]
        if elem_end == len(jsn):
            break
        pos = elem_end+1
        if pos >= len(jsn):
            break
        element_wise += ","
    return f"[{element_wise}]"


# add quotes to unquoted keys, strings and strings in arrays
def quote_object(jsn):
    delimiters = [",", "{"]
    pos = 0
    quoted = ""
    str_list = find_strings(jsn)
    while True:
        cur = pos
        pos = jsn.find(":", pos)
        if pos == -1:
            quoted += jsn[cur:]
            break
        if iq := is_inside_quotes(str_list, pos):
            quoted += jsn[cur:iq]
            pos = iq
            continue
        delim = 0
        for d in delimiters:
            dd = jsn[:pos].rfind(d)
            if dd != -1:
                delim = max(dd, delim)
        key = jsn[delim+1:pos].strip()
        # make sure we arent inside brackets, for multiple inheritence
        if key.find(")") != -1:
            bp = us(jsn[:pos].rfind("("))
            ep = jsn.find(")", delim)
            if bp < delim < ep:
                delim = 0
                for d in delimiters:
                    dd = jsn[:bp].rfind(d)
                    if dd != -1:
                        delim = max(dd, delim)
            key = jsn[delim + 1:pos].strip()
        pos += 1
        next = find_first(jsn, pos, [",", "]", "}"])
        while is_inside_quotes(str_list, next):
            next = find_first(jsn, next+1, [",", "]", "}"])
        # put key in quotes
        value = jsn[pos:next]
        inherit = ""
        if get_value_type(value) == "object":
            inherit = "{"
            pos += 1
            key, inherit_list = get_inherits(key)
            if len(inherit_list) > 0:
                inherit += in_quotes("jsn_inherit") + ": ["
                for p, i in enumerate(inherit_list):
                    if p > 0:
                        inherit += ", "
                    inherit += in_quotes(i.strip())
                inherit += "],"
        qkey = in_quotes(key)
        quoted += jsn[cur:delim+1]
        quoted += qkey
        quoted += ":"
        quoted += inherit
        if get_value_type(value) == "array":
            end = enclose_brackets("[", "]", jsn, pos)
            quoted += quote_array(jsn[pos+1:end-1])
            pos = end
        else:
            value = quote_value(value, pos, next)
            quoted += value[0]
            pos = value[1]
    return quoted


# remove trailing commas from objects and arrays
def remove_trailing_commas(jsn):
    trail = ["}", "]"]
    clean = ""
    for i in range(0, len(jsn)):
        j = i + 1
        char = jsn[i]
        if char == "," and j < len(jsn) and jsn[j] in trail:
            continue
        clean += char
    if clean[len(clean)-1] == ",":
        clean = clean[:-1]
    return clean


# inserts commas in place of newlines \n
def add_new_line_commas(jsn):
    prev_char = ""
    corrected = ""
    ignore_previous = [",", ":", "{", "\n", "\\", "["]
    for char in jsn:
        if char == '\n' and prev_char not in ignore_previous:
            corrected += ','
        corrected += char
        prev_char = char
    return corrected


# inherit dict member wise
def inherit_dict(dest, second):
    for k, v in second.items():
        if type(v) == dict:
            if k not in dest or type(dest[k]) != dict:
                dest[k] = {}
            inherit_dict(dest[k], v)
        elif k not in dest:
            dest[k] = v


# recursively merge dicts member wise
def inherit_dict_recursive(d, d2):
    inherits = []
    for k, v in d.items():
        if k == "jsn_inherit":
            inherits.extend(iter(v))
    if "jsn_inherit" in d.keys():
        d.pop("jsn_inherit", None)
        for i in inherits:
            if i in d2.keys():
                inherit_dict(d, d2[i])
    for k, v in d.items():
        if type(v) == dict:
            inherit_dict_recursive(v, d)


# finds files to import (includes)
def get_imports(jsn, import_dirs):
    imports = []
    bp = jsn.find("{")
    head = jsn[:bp].split("\n")
    has_imports = False
    for i in head:
        if i.find("import") != -1:
            has_imports = True
    if not has_imports:
        return jsn[bp:], imports
    if not import_dirs:
        filedir = os.getcwd()
        print(
            f"WARNING: jsn loads() import file paths will be relative to cwd {filedir}"
        )
        print("\t use load_from_file() for import paths relative to the jsn file.")
    for i in head:
        if i.find("import") != -1:
            stripped = i[len("import"):].strip().strip("\"").strip()
            found = False
            for dir in import_dirs:
                import_path_dir = os.path.join(dir, stripped)
                if os.path.exists(import_path_dir):
                    imports.append(import_path_dir)
                    found = True
                    break
            if not found:
                print(f"ERROR: cannot find import file {stripped}")
    return jsn[bp:], imports


# finds all '${vars}' within a string returning in list [${va}, ${vb}, ...]
def vars_in_string(string):
    pos = 0
    variables = []
    while True:
        sp = string.find("${", pos)
        if sp != -1:
            ep = string.find("}", sp)
            variables.append(string[sp:ep + 1])
            pos = sp + 2
        else:
            break
    return variables
            
    
# resolves "${var}" into a typed value or a token pasted string, handle multiple vars within strings or arrays
def resolve_vars(value, vars):
    value_string = str(value)
    vv = vars_in_string(value_string)
    for count, v in enumerate(vv):
        var_name = v[2:-1]
        if var_name in vars.keys():
            if type(value) == list:
                nl = []
                for i in value:
                    if ri := resolve_vars(i, vars):
                        nl.append(resolve_vars(i, vars))
                    else:
                        nl.append(i)
                return nl
            else:
                if type(vars[var_name]) == str:
                    value = value.replace(v, vars[var_name])
                    if len(vv) == count+1:
                        return value
                else:
                    return vars[var_name]
        else:
            print(platform.system())
            print(json.dumps(vars, indent=4))
            print(value)
            print(f"error: undefined variable '{var_name}'")
            exit(1)
    return None


# replace ${} with variables in vars
def resolve_vars_recursive(d, vars):
    stack_vars = vars.copy()
    if "jsn_vars" in d.keys():
        for vk in d["jsn_vars"].keys():
            stack_vars[vk] = d["jsn_vars"][vk]
    for k in d.keys():
        value = d[k]
        if type(value) == dict:
            resolve_vars_recursive(d[k], stack_vars)
        elif type(value) == list:
            resolved_list = []
            for i in value:
                if ri := resolve_vars(i, stack_vars):
                    resolved_list.append(ri)
                else:
                    resolved_list.append(i)
            d[k] = resolved_list
        elif var := resolve_vars(d[k], stack_vars):
            d[k] = var
    if "jsn_vars" in d.keys():
        d.pop("jsn_vars", None)


# resolve platform specific keys, merging
def resolve_platform_keys_recursive(d, platform_name):
    rm_keys = []
    platform_dict = {}
    for k in d.keys():
        bp = k.find("<")
        ep = k.find(">")
        if bp != -1 and ep != -1:
            key_platform = k[bp+1:ep]
            if key_platform == platform_name:
                key_base = k[:bp]
                platform_dict[key_base] = d[k]
            rm_keys.append(k)
        value = d[k]
        if type(value) == dict:
            resolve_platform_keys_recursive(value, platform_name)
    for k in rm_keys:
        d.pop(k)
    inherit_dict(d, platform_dict)


# check platform name and then recurse through dictionary selecting our current platform keys
def resolve_platform_keys(d):
    name_lookup = {
        "Linux": "linux",
        "Darwin": "mac",
        "Windows": "windows"
    }
    platform_name = "unknown"
    if platform.system() in name_lookup:
        platform_name = name_lookup[platform.system()]
    else:
        print(f"warning: unknown platform system {platform.system()}")
    resolve_platform_keys_recursive(d, platform_name)


# load from file
def load_from_file(filepath, import_dirs):
    jsn_contents = open(filepath).read()
    filepath = os.path.join(os.getcwd(), filepath)
    import_dirs.append(os.path.dirname(filepath))
    return loads(jsn_contents, import_dirs)


# convert jsn to json
def loads(jsn, import_dirs=None):
    jsn, imports = get_imports(jsn, import_dirs)
    jsn = remove_comments(jsn)
    jsn = change_quotes(jsn)
    jsn = trim_whitespace(jsn)
    jsn = add_new_line_commas(jsn)
    jsn = collapse_line_breaks(jsn)
    jsn = clean_src(jsn)
    jsn = quote_object(jsn)
    jsn = remove_trailing_commas(jsn)
    jsn = format(jsn)

    # validate
    try:
        j = json.loads(jsn)
    except:
        jsn_lines = jsn.split("\n")
        for l in range(0, len(jsn_lines)):
            print(f"{str(l + 1)} {jsn_lines[l]}")
        traceback.print_exc()
        exit(1)

    # import
    for i in imports:
        include_dict = loads(open(i, "r").read(), import_dirs)
        inherit_dict(j, include_dict)

    # resolve platform specific keys
    resolve_platform_keys(j)

    # inherit
    inherit_dict_recursive(j, j)

    # resolve vars
    resolve_vars_recursive(j, {})

    return j


# return a list of all imports for this file
def get_import_file_list(filepath, import_dirs=None):
    jsn = open(filepath, "r").read()
    jsn, imports = get_imports(jsn, import_dirs)
    for i in imports:
        recursive_imports = get_import_file_list(i, import_dirs)
        for ri in recursive_imports:
            if ri not in imports:
                imports.append(ri)
    return [os.path.normpath(os.path.join(os.getcwd(), i)) for i in imports]


# convert jsn to json and write to a file
def convert_jsn(info, input_file, output_file):
    print(f"converting: {input_file} to {output_file}")
    with open(output_file, "w+") as output_file:
        jdict = load_from_file(input_file, info.import_dirs)
        if info.print_out:
            print(json.dumps(jdict, indent=4))
        output_file.write(json.dumps(jdict, indent=4))


def main():
    info = parse_args()
    if len(info.inputs) == 0 or not info.output_dir:
        display_help()
        exit(1)
    for i in info.inputs:
        if os.path.isdir(i):
            for root, dirs, files in os.walk(i):
                for file in files:
                    output_file = info.output_dir
                    if not is_file(output_file):
                        output_file = os.path.join(info.output_dir, file)
                        output_file = change_ext(output_file, ".json")
                    create_dir(output_file)
                    convert_jsn(info, os.path.join(root, file), output_file)
        else:
            output_file = info.output_dir
            if not is_file(output_file):
                output_file = os.path.join(info.output_dir, i)
                output_file = change_ext(output_file, ".json")
            create_dir(output_file)
            convert_jsn(info, i, output_file)


# output .jsn files as json,
if __name__ == "__main__":
    main()

