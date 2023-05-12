#!/usr/bin/env python
#
# This script was enspired by https://github.com/microsoft/llvm/blob/master/utils/extract_symbols.py
#
# If you miss more performance (huge libraries), makes sense to check that python script though
#
import sys
import re
import os
import subprocess
import argparse
import glob

# Define functions which extract a list of symbols from a library using several
# different tools. We use subprocess.Popen and yield a symbol at a time instead
# of using subprocess.check_output and returning a list as, especially on
# Windows, waiting for the entire output to be ready can take a significant
# amount of time.
def dumpbin_get_symbols(lib):

    dirs = [
        "C:\\Program Files (x86)\\Microsoft Visual Studio",
        "C:\\Program Files\\Microsoft Visual Studio"
    ]

    dumps = []
    for dir in dirs:
        dumps.extend(glob.glob(os.path.join(dir, "**/dumpbin.exe"), recursive=True))
    dumps = sorted(dumps)

    dumpbin_exe = next((d for d in dumps if os.path.isfile(d)), "")
    if not os.path.isfile(dumpbin_exe):
        print("error: dumpbin utility not found")
        print("error: pmtech/tools/pmbuild_ext/libdef.py is looking for dumpbin.exe in:")
        for d in dirs:
            print(f"error: {d}")
        exit(2)

    process = subprocess.Popen([dumpbin_exe,'/symbols',lib], bufsize=1,
                               stdout=subprocess.PIPE, stdin=subprocess.PIPE,
                               universal_newlines=True)
    process.stdin.close()
    for line in process.stdout:
        if match := re.match("^.+SECT.+External\s+\|\s+(\S+).*$", line):
            yield match[1]
    process.wait()

# MSVC mangles names to ?<identifier_mangling>@<type_mangling>. By examining the
# identifier/type mangling we can decide which symbols could possibly be
# required and which we can discard.
#
# Mangling scheme could be looked in clang/lib/AST/MicrosoftMangle.cpp
#
def should_keep_microsoft_symbol(symbol):
    # Variables with 'g_' prefix (globals)
    if re.search('g_', symbol):
        return f"{symbol} DATA"

    namespaces = [
        "pen",
        "put",
        "Str",
        "ImGui",
        "physics"
    ]

    valid = any(re.search(n, symbol) for n in namespaces)
    if not valid:
        return None

    # mangleVariableEncoding => public static or global member
    return f"{symbol} DATA" if re.search('@@[23]', symbol) else symbol

def extract_symbols(lib):
    symbols = {}
    for symbol1 in dumpbin_get_symbols(lib):
        symbol = should_keep_microsoft_symbol(symbol1)

        if symbol:
            print(f"accepting symbol: {symbol}")
        #else:
        #    print("rejecting symbol: " + symbol1)

        if symbol:
            symbols[symbol] = 1 + symbols.setdefault(symbol,0)
    return symbols


if __name__ == '__main__':
    print ("Executed command:\n  >" + ' '.join(sys.argv) + "\n")

    parser = argparse.ArgumentParser(description='Extracts symbols from static library and saves as a .def file')
    parser.add_argument('libs', metavar='lib', type=str, nargs='+', help='libraries to extract symbols from')
    parser.add_argument('-o', metavar='file', type=str, help='output to file')
    args = parser.parse_args()

    # Get the list of libraries to extract symbols from
    libs = []
    for lib in args.libs:
        # When invoked by cmake the arguments are the cmake target names of the
        # libraries, so we need to add .lib/.a to the end and maybe lib to the
        # start to get the filename. Also allow objects.
        suffixes = ['.lib','.a','.obj','.o']
        if not any(lib.endswith(s) for s in suffixes):
            for s in suffixes:
                if os.path.exists(lib+s):
                    lib = lib+s
                    break
                if os.path.exists(f'lib{lib}{s}'):
                    lib = f'lib{lib}{s}'
                    break
        if not any(lib.endswith(s) for s in suffixes):
            print(f"Don't know what to do with argument {lib}", file=sys.stderr)
            exit(3)
        libs.append(lib)


    # Merge everything into a single dict
    symbols = {}

    for lib in libs:
        lib_symbols = extract_symbols(lib)

        for k,v in list(lib_symbols.items()):
            symbols[k] = v + symbols.setdefault(k,0)

    outfile = open(args.o,'w') if args.o else sys.stdout
    print("EXPORTS", file=outfile)

    for k,v in list(symbols.items()):
        print(k, file=outfile)
        #if k.endswith(" DATA"):
        #    print("__imp_" + k, file=outfile)

    #print("SECTIONS", file=outfile)
    #print("   .idata        READ WRITE", file=outfile)