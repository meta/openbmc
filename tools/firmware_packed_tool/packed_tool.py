#!/usr/bin/python3
# -*- coding: utf-8 -*-

import sys, os, argparse
import json, subprocess
import shutil, urllib.request
import datetime
import socket

pkg_creator_dir = "src"
pkg_creator_tool = "pldm_fwup_pkg_creator.py"
pkg_creator_config = "metadata-example.json"
pkg_branch = "master"
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

compatibleStrings = {}

CONFIG_DIR = os.path.join(BASE_DIR, "config")

def load_config_compatible_strings():
    if not os.path.isdir(CONFIG_DIR):
        return
    for name in sorted(os.listdir(CONFIG_DIR)):
        if not name.endswith(".json"):
            continue
        path = os.path.join(CONFIG_DIR, name)
        try:
            with open(path) as f:
                compatibleStrings.update(json.load(f))
        except (OSError, ValueError) as e:
            print(f"Warning: failed to load {path}: {e}")

load_config_compatible_strings()

def compatible_strings_helper():
    helper = "\n"
    for key, value in compatibleStrings.items():
        helper += f"  {key}\n"
    return helper

def compatible_validation(compatible):
    if compatible not in compatibleStrings:
        print(f"Error: Compatible string '{compatible}' is not supported.")
        print("Supported compatible strings are:")
        print(compatible_strings_helper())
        sys.exit(1)

def input_validation():
    if not os.path.isfile(args.input):
        print(f"Error: Input '{args.input}' does not exist or is not a file.")
        sys.exit(1)

def has_access(url, timeout=1):
    # Check if the URL is accessible
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return response.status == 200
    except Exception:
        print(f"Access check failed: {e}")
        print(f"Error: Unable to access {url}. Please check your internet connection or the URL.")
        sys.exit(1)

def download_file(url, filename, timeout=10):
    print(f"📥 Downloading to {filename} ...")
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response, open(filename, 'wb') as out_file:
            block_size = 8192
            while True:
                chunk = response.read(block_size)
                if not chunk:
                    break
                out_file.write(chunk)
        print(f"✅ Downloaded {filename}")
    except socket.timeout:
        print("❌ Download timed out.")
        sys.exit(1)
    except Exception as e:
        print(f"❌ Failed to download: {e}")
        sys.exit(1)

def tool_init():
    os.makedirs(f"{BASE_DIR}/{pkg_creator_dir}", exist_ok=True)
    tool = f"{BASE_DIR}/{pkg_creator_dir}/{pkg_creator_tool}"

    if not os.path.exists(tool):
        print("Tool not found, downloading...")

        print(f"Downloading {pkg_creator_tool} from GitHub branch {pkg_branch}...")
        url = f"https://raw.githubusercontent.com/openbmc/pldm/{pkg_branch}/tools/fw-update/{pkg_creator_tool}"
        has_access(url)
        download_file(url, tool)

def update_config(config, version, compatible):
    metadata = json.load(open(f"{BASE_DIR}/{pkg_creator_dir}/{pkg_creator_config}"))
    del metadata['FirmwareDeviceIdentificationArea'][1:]
    del metadata['ComponentImageInformationArea'][1:]
    metadata['PackageHeaderInformation']['PackageReleaseDateTime'] = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    metadata['PackageHeaderInformation']['PackageVersionString'] = version
    metadata['FirmwareDeviceIdentificationArea'][0]['ApplicableComponents'] = [0]
    metadata['FirmwareDeviceIdentificationArea'][0]['ComponentImageSetVersionString'] = version
    metadata['ComponentImageInformationArea'][0]['ComponentVersionString'] = version
    descriptors = metadata['FirmwareDeviceIdentificationArea'][0]['Descriptors']
    for descriptor in descriptors:
        if descriptor['DescriptorType'] == 1:
            descriptor['DescriptorData'] = "15A00000"
        if descriptor['DescriptorType'] == 65535:
            descriptor['VendorDefinedDescriptorTitleString'] = compatibleStrings[compatible]
    json.dump(metadata, open(f"{BASE_DIR}/{pkg_creator_dir}/{pkg_creator_config}", "w"), indent=4)

def pack_firmware(compatible, version, input_file, output_file):

    update_config(pkg_creator_config, version, compatible)
    cmd = [
        sys.executable,
        f"{BASE_DIR}/{pkg_creator_dir}/{pkg_creator_tool}",
        f"{BASE_DIR}/{pkg_creator_dir}/{output_file}",
        f"{BASE_DIR}/{pkg_creator_dir}/{pkg_creator_config}",
        input_file,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.isfile(f"{BASE_DIR}/{pkg_creator_dir}/{output_file}"):
        print("Error: pldm_fwup_pkg_creator failed to create the package.")
        print(result.stdout)
        print(result.stderr)
        sys.exit(1)
    print(result.stderr)
    shutil.move(f"{BASE_DIR}/{pkg_creator_dir}/{output_file}", f"{BASE_DIR}/{output_file}")
    print("Successfully packed firmware : %s" % os.path.basename(input_file))
    print("Compatible string: %s" % compatible)
    print("Firmware version: %s" % version)
    print("Output file: %s" % output_file)

if __name__ == "__main__":
    tool_init()

    parser = argparse.ArgumentParser(
        description='LF-OpenBMC firmware packed tool',
        formatter_class=argparse.RawTextHelpFormatter)

    parser.add_argument(
        '-c', '--compatible',
        type=str,
        required=True,
        help='Target device hardware compatible string, e.g. %s' % compatible_strings_helper())

    parser.add_argument(
        '-v', '--version',
        type=str,
        required=True,
        help='Firmware version string.')

    parser.add_argument(
        '-i', '--input',
        type=str,
        required=True,
        help='Input file name for the firmware files')

    parser.add_argument(
        '-o', '--output',
        type=str,
        help='Output file name for the packed firmware, default is <compatible>_<version>.pldm')

    args = parser.parse_args()
    compatible_validation(args.compatible)
    input_validation()
    output = args.output if args.output else f"{args.compatible}_{args.version}.pldm"
    pack_firmware(args.compatible, args.version, args.input, output)



