import argparse
import glob
import os
import re
import subprocess
import sys
import zipfile

from tools import utils


def main(args):
    parser = argparse.ArgumentParser(description='Automated release script for Vali.')
    parser.add_argument('--target', required=True, help='release target (vmdk, img, live)')
    parser.add_argument('--scheme', required=True, help='disk fs scheme (mbr, gpt)')
    parser.add_argument('--local', default=False, action='store_true', help='skip the unzip step (debug)')
    pargs = parser.parse_args()

    # Are we invoked by teamcity? Then 4 zip packages should be available
    # vali-<version>-<arch>.zip
    # vali-sdk-<version>-<arch>.zip
    # vali-ddk-<version>-<arch>.zip
    # vali-apps-<version>-<arch>.zip
    main_path = os.path.join(os.getcwd(), 'vali')
    sdk_path = os.path.join(os.getcwd(), 'vali-sdk')
    ddk_path = os.path.join(os.getcwd(), 'vali-ddk')
    app_path = os.path.join(os.getcwd(), 'vali-apps')
    version_parts = []
    arch = ''

    if not pargs.local:
        main_zip_regex = re.compile('vali-([0-9]+).([0-9]+).([0-9]+)-([0-9a-zA-Z]+).zip', re.IGNORECASE)
        sdk_zip_regex = re.compile('vali-sdk-([0-9]+).([0-9]+).([0-9]+)-([0-9a-zA-Z]+).zip', re.IGNORECASE)
        ddk_zip_regex = re.compile('vali-ddk-([0-9]+).([0-9]+).([0-9]+)-([0-9a-zA-Z]+).zip', re.IGNORECASE)
        ddk_app_regex = re.compile('vali-apps-([0-9]+).([0-9]+).([0-9]+)-([0-9a-zA-Z]+).zip', re.IGNORECASE)

        # Unzip all required zip files
        for file in glob.glob('*.zip'):
            print("Detected zip file: " + file)
            m = main_zip_regex.match(file)
            if m:
                print("Detected primary zip file")
                version_parts.append(m.group(1))
                version_parts.append(m.group(2))
                version_parts.append(m.group(3))
                arch = m.group(4)
                utils.unzip_file(file, main_path)
            else:
                m = sdk_zip_regex.match(file)
                if m:
                    print("Detected sdk zip file")
                    utils.unzip_file(file, sdk_path)
                else:
                    m = ddk_zip_regex.match(file)
                    if m:
                        print("Detected ddk zip file")
                        utils.unzip_file(file, ddk_path)
                    else:
                        m = ddk_app_regex.match(file)
                        if m:
                            print("Detected apps zip file")
                            utils.unzip_file(file, app_path)
                        else:
                            print("Detected unusued zip file, removing")
                            os.unlink(file)

    # Define the harddisk structure
    deployment_folder = os.path.join(os.getcwd(), 'deploy')
    deployment_hdd_folder = os.path.join(deployment_folder, 'hdd')
    deployment_system_folder = os.path.join(deployment_hdd_folder, 'system')
    deployment_shared_folder = os.path.join(deployment_hdd_folder, 'shared')

    # Create the deployment folder and initial directory structure
    utils.empty_folder_if_exist(deployment_folder)
    os.mkdir(deployment_hdd_folder)
    os.mkdir(deployment_system_folder)
    os.mkdir(deployment_shared_folder)

    # Copy resources from main_path/resources/system and main_path/resources/shared
    utils.recursive_copy_files(os.path.join(main_path, 'resources', 'system'), deployment_system_folder)
    utils.recursive_copy_files(os.path.join(main_path, 'resources', 'shared'), deployment_shared_folder)
    utils.copy_files(main_path, deployment_folder, pattern='*.sys')

    # Copy kernel and ramdisk to system folder
    utils.copy_files(main_path, deployment_system_folder, pattern='*.mos')

    # Copy all sdk libraries and binaries
    if os.path.exists(sdk_path):
        utils.recursive_copy_files(sdk_path, deployment_shared_folder)

    # Copy all application libraries and binaries
    if os.path.exists(app_path):
        utils.recursive_copy_files(app_path, deployment_shared_folder)

    # Build the requested image file
    diskutility_path = os.path.join(main_path, 'diskutility')
    os.chmod(diskutility_path, 509)
    p = subprocess.Popen([diskutility_path, '-auto', '-target', pargs.target, '-scheme', pargs.scheme])
    p.wait()

    # Delete the deployment folder and resource folders as we are now done
    utils.delete_folder_if_exists(deployment_folder)
    utils.delete_folder_if_exists(main_path)
    utils.delete_folder_if_exists(sdk_path)
    utils.delete_folder_if_exists(ddk_path)
    utils.delete_folder_if_exists(app_path)

    # Zip and remove the disk image
    if len(version_parts) == 0:
        zip_file_name = 'vali-release-local-' + pargs.target + '.zip'
    else:
        zip_file_name = 'vali-release-' + version_parts[0] + '.' + version_parts[1] + '.' + version_parts[
            2] + '-' + arch + '-' + pargs.target + '.zip'
    zipf = zipfile.ZipFile(zip_file_name, 'w', zipfile.ZIP_DEFLATED)
    zipf.write('mollenos.' + pargs.target)
    zipf.close()
    os.unlink('mollenos.' + pargs.target)


if __name__ == "__main__":
    main(sys.argv)
