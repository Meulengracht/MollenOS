import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import zipfile


def delete_folder_if_exists(folder_path):
    if os.path.exists(folder_path):
        try:
            shutil.rmtree(folder_path)
        except Exception as e:
            print(folder_path + ': ' + str(e))


def empty_folder_if_exist(folder_path):
    print("Unpack to " + folder_path)
    if os.path.exists(folder_path):
        print("Path exists already, emptying it")
        for the_file in os.listdir(folder_path):
            file_path = os.path.join(folder_path, the_file)
            try:
                if os.path.isfile(file_path):
                    os.unlink(file_path)
                elif os.path.isdir(file_path):
                    shutil.rmtree(file_path)
            except Exception as e:
                print(str(e))
    else:
        os.mkdir(folder_path)


def unzip_file(zip_path, output_dir):
    empty_folder_if_exist(output_dir)
    zip_i = zipfile.ZipFile(zip_path, 'r')
    zip_i.extractall(output_dir)
    zip_i.close()
    os.unlink(zip_path)


def add_folder_to_zip(zip_file, folder):
    for file in os.listdir(folder):
        full_path = os.path.join(folder, file)
        if os.path.isfile(full_path):
            print('File added: ' + str(full_path))
            zip_file.write(full_path)
        elif os.path.isdir(full_path):
            print('Entering folder: ' + str(full_path))
            add_folder_to_zip(zip_file, full_path)


def copy_files(source_path, destination_path, pattern='*', overwrite=False):
    """
    Copies files from source  to destination directory.
    :param pattern: the file pattern to copy
    :param source_path: source directory
    :param destination_path: destination directory
    :param overwrite if True all files will be overwritten otherwise skip if file exist
    :return: count of copied files
    """
    files_count = 0
    if not os.path.exists(destination_path):
        os.mkdir(destination_path)
    items = glob.glob(source_path + '/' + pattern)
    for item in items:
        if not os.path.isdir(item):
            file = os.path.join(destination_path, item.split('/')[-1])
            if not os.path.exists(file) or overwrite:
                shutil.copyfile(item, file)
                files_count += 1
    return files_count


def recursive_copy_files(source_path, destination_path, pattern='*', overwrite=False):
    """
    Recursive copies files from source  to destination directory.
    :param pattern: the file pattern to copy
    :param source_path: source directory
    :param destination_path: destination directory
    :param overwrite if True all files will be overwritten otherwise skip if file exist
    :return: count of copied files
    """
    files_count = 0
    if not os.path.exists(destination_path):
        os.mkdir(destination_path)
    items = glob.glob(source_path + '/' + pattern)
    for item in items:
        if os.path.isdir(item):
            path = os.path.join(destination_path, item.split('/')[-1])
            files_count += recursive_copy_files(source_path=item, destination_path=path, pattern=pattern,
                                                overwrite=overwrite)
        else:
            file = os.path.join(destination_path, item.split('/')[-1])
            if not os.path.exists(file) or overwrite:
                shutil.copyfile(item, file)
                files_count += 1
    return files_count


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Installation utilities for building and releasing Vali.')
    parser.add_argument('--cp', default=False, action='store_true',
                        help='invoke script in copy-file mode, use --source, --dest and --pattern')
    parser.add_argument('--create-zip', default=False, action='store_true',
                        help='create a release zip from a directory with the current versioning')

    cpArguments = parser.add_argument_group('cp')
    cpArguments.add_argument('--source', default=None, help='source directory for cp')
    cpArguments.add_argument('--dest', default=None, help='destination directory for cp')
    cpArguments.add_argument('--pattern', default="*", help='the file pattern to use for cp')

    czArguments = parser.add_argument_group('create-zip')
    czArguments.add_argument('--zip-dirs', default=None, help='source directory for create-zip')
    czArguments.add_argument('--zip-out', default=None, help='zip file output path')

    pargs = parser.parse_args()

    if pargs.cp:
        if pargs.source and pargs.dest:
            copy_files(pargs.source, pargs.dest, pargs.pattern, overwrite=True)
            sys.exit(0)
        else:
            print("utils: missing arg --source or --dest")
    elif pargs.create_zip:
        output_regex = re.compile('([0-9]+).([0-9]+).([0-9]+)', re.IGNORECASE)

        if pargs.zip_dirs and pargs.zip_out:
            zip_version = ""
            zip_out = pargs.zip_out
            print("utils: creating zip file " + zip_out)
            zipf = zipfile.ZipFile(zip_out, 'w')
            zip_paths = pargs.zip_dirs.split(',')
            for zip_dir in zip_paths:
                add_folder_to_zip(zipf, zip_dir)
            zipf.close()
            sys.exit(0)
        else:
            print("utils: missing arg --zip-dir or --zip-out")
    else:
        print("utils: either --cp or --create-zip must be given as arguments")
    sys.exit(-1)
