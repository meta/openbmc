# Build from pyproject.toml to avoid deprecated setup.py requirement resolution.
inherit python_setuptools_build_meta

DEPENDS += "python3-wheel-native"

# The base recipe's setuptools3 class already exported its legacy compile task.
do_compile() {
    python_pep517_do_compile
}
