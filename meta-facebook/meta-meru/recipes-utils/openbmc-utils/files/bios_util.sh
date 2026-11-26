#!/bin/bash

# shellcheck disable=SC1091
. /usr/local/bin/openbmc-utils.sh

# Check if another fw upgrade is ongoing
check_fwupgrade_running

trap cleanup INT TERM QUIT EXIT

SPI_CHANNEL="1.0"
DRIVER=""
BIOS_UTIL_NAME="$0"

usage() {
    program=$(basename "$BIOS_UTIL_NAME")
    echo "Usage:"
    echo "$program <action> [<bios file>] [--partition <partition 0> ... <partition N>]"
    echo "      <action> : read, write, verify, erase, fullread, fullwrite"
    echo "      [<bios file>] : file to read to/write from"
    echo "          Required for all actions except erase"
    echo "      [<partition>] : partition(s) of layout file; defaults to image"
    echo "          If <partition> is given, <action> must be"
    echo "          read, write, program, verify, or erase"
}

cleanup() {
    disconnect_program_paths
}

ORIG_POWER_STATE=$(userver_power_is_on; echo $?)

disconnect_program_paths() {
    unbind_spi_nor_driver "$SPI_CHANNEL"
    gpio_set_value ABOOT_GRAB 0
    gpio_set_value SW_CPLD_JTAG_SEL 0
    gpio_set_value SW_SPI_SEL 0

    if wedge_cpu_needs_extra_spi_gpios; then
        gpio_set_value SW_SPI_WP_L 0
        gpio_set_value SW_SPI_HOLD_L 1
    fi

    if [ "$ORIG_POWER_STATE" -eq 0 ]; then
        userver_power_on
    fi
}

connect_spi() {
    devmem_clear_bit 0x1e6e2438 8 # SPI1CS1 Function disable
    if [ "$ORIG_POWER_STATE" -eq 0 ]; then
        userver_power_off
    fi
    gpio_set_value ABOOT_GRAB 1
    gpio_set_value SW_CPLD_JTAG_SEL 0
    gpio_set_value SW_SPI_SEL 0

    if wedge_cpu_needs_extra_spi_gpios; then
        gpio_set_value SW_SPI_WP_L 1
        gpio_set_value SW_SPI_HOLD_L 1
    fi
}

select_spi_driver() {
    # Select the SPI driver
    # For GD25LQ - we want to use flashrom, otherwise spinor
    unbind_spi_nor_driver "$SPI_CHANNEL"
    local partitions=("image")
    if do_spi_image "gd25lq256d" "FLASHROM" "$SPI_CHANNEL" "BIOS" "IDENTIFY" "${partitions[@]}"; then
        DRIVER="FLASHROM"
        unbind_spi_nor_driver "$SPI_CHANNEL"
    else
        DRIVER="SPINOR"
        bind_spi_nor_driver "$SPI_CHANNEL"
    fi
}

narg_err() {
    echo "Invalid number of arguments"
    usage
    exit 1
}

BIOS_PARTITIONS=("image")
if wedge_cpu_needs_extra_spi_gpios; then
    BIOS_PARTITIONS=("psp" "bios")
fi

bios_action="$1"
case "${bios_action^^}" in
    FULLREAD|FULLWRITE)
        if [ $# -ne 2 ]; then
            narg_err
        fi
        partitions=("total")
        target_image="$2"
        ;;
    READ|VERIFY|WRITE|PROGRAM)
        if [ $# -eq 2 ]; then
            partitions=("${BIOS_PARTITIONS[@]}")
            target_image="$2"
        elif [ $# -ge 4 ] && [ "$3" == "--partition" ]; then
            target_image="$2"
            shift 3
            partitions=("$@")
        else
            narg_err
        fi
        ;;
    ERASE)
        if [ $# -eq 1 ]; then
            partitions=("${BIOS_PARTITIONS[@]}")
        elif [ $# -ge 3 ] && [ "$2" == "--partition" ]; then
            shift 2
            partitions=("$@")
        else
            narg_err
        fi
        target_image=""
        ;;
    *)
        echo "Unknown action: $1"
        usage
        exit 1
        ;;
esac

connect_spi
select_spi_driver
do_spi_image "$target_image" "$DRIVER" "$SPI_CHANNEL" "BIOS" "$bios_action" "${partitions[@]}"
