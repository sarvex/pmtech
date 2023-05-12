import os

# this function takes input from pmtech tests which list all files accessed by each example program
# this information is used to reduce data sizes for wasm demos and only include what is absolutely necessary
if __name__ == "__main__":
    for file in os.listdir(os.path.join(os.getcwd(), "generated")):
        if not file.endswith(".txt"):
            continue
        src = os.path.join(os.getcwd(), "generated", file)
        files = open(src, "r").read()
        data_file_list = files.split("\n")
        data_file_list = data_file_list[:len(data_file_list)-2]
        accepted_data_file_list = []
        exclusive_data_dirs = [
            "data/audio/",
            "data/models/",
            "data/textures"
        ]
        for data_file in data_file_list:
            if data_file.endswith(".dep"):
                continue
            accepted_data_file_list.extend(
                data_file
                for dd in exclusive_data_dirs
                if data_file.startswith(dd)
            )
        shared_data_dirs = [
            "data/configs/",
            "data/fonts/",
            "data/pmfx/",
            "data/scene/"
        ]
        cmd_str = "".join(f"-s --preload-file {sd} " for sd in shared_data_dirs)
        for df in accepted_data_file_list:
            cmd_str += f"-s --preload-file {df} "
        print(file)
        with open(os.path.basename(file), "w") as output:
            output.write(cmd_str)
