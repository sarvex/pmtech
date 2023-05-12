import platform
import os
import shutil
import cgu


def get_platform_name_args(args):
    for i in range(1, len(args)):
        if "-platform" in args[i]:
            return args[i + 1]
    if os.name == "posix":
        return "linux" if platform.system() == "Linux" else "osx"
    else:
        return "win32"


def get_platform_name():
    if os.name == "posix":
        return "linux" if platform.system() == "Linux" else "osx"
    else:
        return "win32"


def correct_path(path):
    return path.replace("/", "\\") if os.name == "nt" else path


def sanitize_file_path(path):
    path = path.replace("/", os.sep)
    path = path.replace("\\", os.sep)
    return path


def get_platform_exe_ext(platform):
    return ".exe" if platform == "win32" else ""


def get_platform_exe_run(platform):
    return "" if platform == "win32" else "./"


# create a new dir if it doesnt already exist and not throw an exception
def create_dir(dst_file):
    dir = os.path.dirname(dst_file)
    if not os.path.exists(dir):
        os.makedirs(dir)


# copy src_file to dst_file creating directory if necessary
def copy_file_create_dir(src_file, dst_file):
    if not os.path.exists(src_file):
        print(f"[error] {src_file} does not exist!")
        return False
    try:
        create_dir(dst_file)
        src_file = os.path.normpath(src_file)
        dst_file = os.path.normpath(dst_file)
        shutil.copyfile(src_file, dst_file)
        print(f"copy {src_file} to {dst_file}")
        return True
    except Exception as e:
        print(f"[error] failed to copy {src_file}")
        return False


# copy src_file to dst_file creating directory if necessary only if the src file is newer than dst
def copy_file_create_dir_if_newer(src_file, dst_file):
    if not os.path.exists(src_file):
        print(f"[error] src_file {src_file} does not exist!")
        return
    if os.path.exists(dst_file) and os.path.getmtime(
        dst_file
    ) >= os.path.getmtime(src_file):
        print(f"{dst_file} up-to-date")
        return
    copy_file_create_dir(src_file, dst_file)


# member wise merge 2 dicts, second will overwrite dest
def merge_dicts(dest, second):
    for k, v in second.items():
        if type(v) == dict:
            if k not in dest or type(dest[k]) != dict:
                dest[k] = {}
            merge_dicts(dest[k], v)
        else:
            dest[k] = v


# change file extension to ext
def change_ext(file, ext):
    return os.path.splitext(file)[0] + ext


if __name__ == "__main__":
    print("util")
