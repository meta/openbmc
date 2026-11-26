#!/bin/bash

# shellcheck disable=SC1091
# shellcheck disable=SC2012
# shellcheck disable=SC2034
# shellcheck disable=SC2329
. /usr/local/bin/openbmc-utils.sh

# Check if another fw upgrade is ongoing
check_fwupgrade_running

trap cleanup INT TERM QUIT EXIT

usage() {
    program=$(basename "$0")
    echo "Usage:"
    echo "$program <fpga> <action> <fpga file>"
    echo "      <fpga> : scm"
    echo "      <action> : program, verify"
    echo "      <fpga file> : file to program/verify"
    exit 1
}

cleanup() {
    disconnect_program_paths
}

disconnect_program_paths() {
    gpio_set_value BMC_LITE_L 0
    gpio_set_value SW_JTAG_SEL 1
    gpio_set_value JTAG_TRST_L 0
}

connect_scm_jtag() {
    gpio_set_value BMC_LITE_L 1
    gpio_set_value SW_JTAG_SEL 0
    gpio_set_value JTAG_TRST_L 1
}

check_revision() {
    cpu_id=$(wedge_cpu_id)
    if [ "${cpu_id}" != "${BOARD_ID_MERU800B}" ]; then
        return
    fi

    # verify the fpga file
    fpga_file="$1"
    revision=$(( $(awk '$0 ~ /NOTE.{1,30}A_REVISION/ {print $3}' "$fpga_file" |
                   sed 's/[";]//g') ))

    if [ "$revision" -lt 3 ]; then
        if ! wedge_is_scm_p1; then
            echo "P1 FPGA FILE not compatible with P4 Hardware!"
            exit 1
        fi
    else
        if wedge_is_scm_p1; then
            echo "P4 FPGA FILE not compatible with P1 Hardware!"
            exit 1
        fi
    fi
}

do_scm() {
    check_revision "$2"
    connect_scm_jtag
    jam -l/usr/lib/libcpldupdate_dll_ioctl.so -v -a"${1^^}" "$2" \
      --ioctl IOCBITBANG
}

if [ $# -lt 2 ]; then
    usage
fi

case "$1" in
   scm) shift 1
      case "${1^^}" in
          PROGRAM|VERIFY)
          do_scm "$@"
          exit 0
          ;;
      esac
      ;;
esac

usage
