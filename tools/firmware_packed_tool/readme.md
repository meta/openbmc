# 🛠️ LF-OpenBMC Firmware Pack Tool

A Python command-line utility for genTo add a new device, create a new `.json` file in `config/`.

## 🔧 Exampleting PLDM-compliant firmware update packages.

This tool:
- Updates metadata in a JSON template
- Injects the correct `compatible` string and firmware `version`
- Invokes the `pldm_fwup_pkg_creator.py` script to create the firmware package

---

## 📦 Features

- Supports multiple device types (CPLD, BIOS, VR, Retimer, etc.)
- Validates input firmware file and compatible string
- Automatically names the output `.pldm` package if not specified

---

## 📦 Installation

Before running the tool, make sure required Python dependencies are installed:

```bash
python3 -m pip install bitarray
```

---

## 📁 Prerequisites

- `src/metadata-example.json` must exist. The tool does not download it.
- If `src/pldm_fwup_pkg_creator.py` is missing, the tool downloads it from the `master` branch of `openbmc/pldm`.

---

## 🧾 Usage

```bash
python3 packed_tool.py -c <compatible> -i <input_file> -v <version> [-o <output_file>]
```
---

## Required arguments
-c, --compatible: Target device hardware compatible string. Supported values include:
- harma-mb-cpld
- harma-scm-cpld
- harma-aegis-cpld
- harma-bios
- harma-isl-vcore0
- harma-isl-vcore1
- harma-isl-pvdd11
- harma-xdpe-vcore0
- harma-xdpe-vcore1
- harma-xdpe-pvdd11
- harma-retimer

-i, --input: Path to the input binary firmware file.
-v, --version: Firmware version string.

Optional arguments

-o, --output: Custom output filename (default: <compatible>_<version>.pldm)

## � Config

Compatible strings are loaded from every `.json` file in the `config/` directory. Each file is a flat JSON object that maps a compatible name to its compatible string, for example:

```json
{
    "<compatible-name>" : "<compatible-string>"
}
```

To add a new device, create a new `.json` file in `config/`. If a key is defined in more than one file, the value from the file loaded last wins.

## �🔧 Example

``` bash
python3 packed_tool.py \
  -c harma-mb-cpld \
  -i my_cpld_firmware.bin \
  -v 2.1.0
```

This will:
Set the version to 2.1.0 in the metadata
Embed the correct compatible string for harma-mb-cpld
Set PackageReleaseDateTime to the current time (format: YYYY-MM-DD HH:MM:SS)
Produce the package: harma-mb-cpld_2.1.0.pldm
