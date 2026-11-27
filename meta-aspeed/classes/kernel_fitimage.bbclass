# Common code for generating u-boot FIT binary images for u-boot.
# Also generate an image for the OpenBMC ROM and upgradable flash.
#
# Copyright (C) 2016-Present, Facebook, Inc.
# Inherit u-boot classes if legacy uboot images are in use.
inherit image_types

# Make Rocko images work just like they would in krogoth
# That means, let conversion on u-boot call into oe_mkimage so
# we may do our fit conversions.
# TODO:
# More up-stream-worthy method is to use the chaining of conversions.
CONVERSION_CMD:u-boot = "oe_mkimage ${IMAGE_NAME}${IMAGE_NAME_SUFFIX}.${type} none"

require recipes-bsp/u-boot/verified-boot.inc

# Changing the image compression from gz to lzma achieves 30% saving (~3M).
IMAGE_FSTYPES += "cpio.lzma.u-boot"

# Algorithm used to compress the kernel in the FIT: "none", "gzip" or "lzma".
# Ignored for zImage, uImage and bzImage, which decompress themselves.
FIT_KERNEL_COMP_ALG ??= "none"

# U-Boot is placed at an absolute position relative to the start of the first
# U-Boot FIT. This value, like most others, depends on U-Boot machine
# configurations.
ROM_UBOOT_POSITION ?= "0x4000"
ROM_UBOOT_LOADADDRESS ?= "0x28084000"

# Set sizes and offsets in KB:
#   For kernel and rootfs: 16384
#   For kernel only: 2690
FLASH_SIZE ?= "16384"
FIT_MAX_SIZE = "28442624"
RECOVERY_MAX_SIZE = "303104"
SIGNED_UBOOT_MAX_SIZE = "393216"
SPL_MAX_SIZE = "86016"

#
# Default maximum u-boot partition size: 380KB, and u-boot checksum is
# stored at offset 380KB in the flash image.
#
UBOOT_PART_MAX_BYTES ?= "389120"
UBOOT_CKSUM_OFFSET_KB ?= "380"

#
# Default offset (in KB) of FIT (kernel, rootfs and dtb) in flash image.
#
FLASH_FIT_OFFSET_KB ?= "512"

UBOOT_SOURCE ?= "${DEPLOY_DIR_IMAGE}/u-boot-${MACHINE}.${UBOOT_SUFFIX}"
FIT_SOURCE ?= "${STAGING_DIR_HOST}/etc/fit-${MACHINE}.its"

FIT[vardepsexclude] = "DATETIME"
FIT ?= "fit-${MACHINE}-${DATETIME}.itb"
FIT_LINK ?= "fit-${MACHINE}.itb"

FLASH_IMAGE[vardepsexclude] += "DATETIME"
FLASH_IMAGE ?= "flash-${MACHINE}-${DATETIME}"
FLASH_IMAGE_LINK ?= "flash-${MACHINE}"

# ROM-based boot variables
UBOOT_RECOVERY_SOURCE ?= "${DEPLOY_DIR_IMAGE}/u-boot-recovery-${MACHINE}.${UBOOT_SUFFIX}"
UBOOT_SPL_SOURCE ?= "${DEPLOY_DIR_IMAGE}/u-boot-spl-${MACHINE}.${UBOOT_SUFFIX}"
UBOOT_FIT_SOURCE ?= "${STAGING_DIR_HOST}/etc/u-boot-fit-${MACHINE}.its"

UBOOT_FIT[vardepsexclude] = "DATETIME"
UBOOT_FIT ?= "u-boot-fit-${MACHINE}-${DATETIME}.itb"
UBOOT_FIT_LINK ?= "u-boot-fit-${MACHINE}.itb"
DEPENDS:append = " dtc-native"

IMAGE_PREPROCESS_COMMAND += " generate_data_mount_dir ; "
IMAGE_POSTPROCESS_COMMAND += " flash_image_generate ; "

generate_data_mount_dir() {
    mkdir -p "${IMAGE_ROOTFS}/mnt/data"
}

flash_image_generate() {
    FIT_DESTINATION="${DEPLOY_DIR_IMAGE}/${FIT_LINK}"
    FLASH_IMAGE_DESTINATION="${DEPLOY_DIR_IMAGE}/${FLASH_IMAGE}"
    UBOOT_FIT_DESTINATION="${DEPLOY_DIR_IMAGE}/${UBOOT_FIT_LINK}"

    if [ ! -f $UBOOT_SOURCE ]; then
        echo "U-boot file ${UBOOT_SOURCE} does not exist"
        return 1
    fi

    if [ ! -f $FIT_DESTINATION ]; then
        echo "FIT binary file ${FIT_DESTINATION} does not exist"
        return 1
    fi

    rm -rf $FLASH_IMAGE_DESTINATION
    dd if=/dev/zero of=${FLASH_IMAGE_DESTINATION} bs=1k count=${FLASH_SIZE}

    if [ "x${VERIFIED_BOOT}" != "x" ] ; then
        FLASH_UBOOT_RECOVERY_OFFSET=84
        FLASH_UBOOT_OFFSET=512
        FLASH_FIT_OFFSET=896

        if [ $(stat -L -c%s ${UBOOT_RECOVERY_SOURCE}) -gt ${RECOVERY_MAX_SIZE} ]; then
            echo "Recovery is too large to fit in the partition"
            return 1
        fi

        # Write the recovery U-Boot.
        dd if=${UBOOT_RECOVERY_SOURCE} of=${FLASH_IMAGE_DESTINATION} bs=1k seek=${FLASH_UBOOT_RECOVERY_OFFSET} conv=notrunc

        if [ $(stat -L -c%s ${UBOOT_FIT_DESTINATION}) -gt ${SIGNED_UBOOT_MAX_SIZE} ]; then
            echo "Signed u-boot FIT too large to fit into partition"
            return 1
        fi

        # Write an intermediate FIT containing only U-Boot.
        dd if=${UBOOT_FIT_DESTINATION} of=${FLASH_IMAGE_DESTINATION} bs=1k seek=${FLASH_UBOOT_OFFSET} conv=notrunc
    else
        FLASH_UBOOT_OFFSET=0
        FLASH_FIT_OFFSET=${FLASH_FIT_OFFSET_KB}

        if [ $(stat -L -c%s ${UBOOT_SOURCE}) -gt ${UBOOT_PART_MAX_BYTES} ]; then
            echo "U-boot is too large to fit into partition"
            return 1
        fi
        # Write U-Boot directly to the start of the flash.
        dd if=${UBOOT_SOURCE} of=${FLASH_IMAGE_DESTINATION} bs=1k seek=${FLASH_UBOOT_OFFSET} conv=notrunc
    fi


    if [ $(stat -L -c%s ${FIT_DESTINATION}) -gt ${FIT_MAX_SIZE} ]; then
        echo "FIT is too large to fit into its partition"
        echo "    $(stat -L -c%s ${FIT_DESTINATION}) > ${FIT_MAX_SIZE}"
        return 1
    fi

    dd if=${FIT_DESTINATION} of=${FLASH_IMAGE_DESTINATION} bs=1k seek=${FLASH_FIT_OFFSET} conv=notrunc

    if [ "x${VERIFIED_BOOT}" != "x" ] ; then
      if [ $(stat -L -c%s ${UBOOT_SPL_SOURCE}) -gt ${SPL_MAX_SIZE} ]; then
          echo "SPL is too large to fit into the partition"
          return 1
      fi
      dd if=${UBOOT_SPL_SOURCE} of=${FLASH_IMAGE_DESTINATION} bs=1k seek=0 conv=notrunc
    fi

    # Generate MD5sums and store in image.
    echo "{\"$(dd if=${FLASH_IMAGE_DESTINATION} bs=1k count=${UBOOT_CKSUM_OFFSET_KB} 2> /dev/null | md5sum | awk '{print $1}')\": \"Built: $(date)\"}" > ./tmp.md5
    dd if=./tmp.md5 of=${FLASH_IMAGE_DESTINATION} bs=1k count=4 seek=${UBOOT_CKSUM_OFFSET_KB} conv=notrunc
    rm -f ./tmp.md5

    ln -sf ${FLASH_IMAGE} ${DEPLOY_DIR_IMAGE}/${FLASH_IMAGE_LINK}
}

oe_mkimage() {
    FIT_DESTINATION="${DEPLOY_DIR_IMAGE}/${FIT}"
    UBOOT_FIT_DESTINATION="${DEPLOY_DIR_IMAGE}/${UBOOT_FIT}"

    if [ "x${VERIFIED_BOOT}" != "x" ] ; then
      rm -f ${UBOOT_FIT_SOURCE}
      fitimage_emit_fit_header ${UBOOT_FIT_SOURCE}

      # Step 1: Prepare a firmware image section
      fitimage_emit_section_maint ${UBOOT_FIT_SOURCE} imagestart
      fitimage_emit_section_firmware ${UBOOT_FIT_SOURCE} 1 "${UBOOT_SOURCE}"
      fitimage_emit_section_maint ${UBOOT_FIT_SOURCE} sectend

      # Step 2: Prepare a configurations section
      fitimage_emit_section_maint ${UBOOT_FIT_SOURCE} confstart
      fitimage_emit_section_firmware_config ${UBOOT_FIT_SOURCE} 1
      fitimage_emit_section_maint ${UBOOT_FIT_SOURCE} sectend
      fitimage_emit_section_maint ${UBOOT_FIT_SOURCE} fitend

      mkimage -f ${UBOOT_FIT_SOURCE} -E -p ${ROM_UBOOT_POSITION} ${UBOOT_FIT_DESTINATION}
      ln -sf ${UBOOT_FIT} ${DEPLOY_DIR_IMAGE}/${UBOOT_FIT_LINK}
    fi

    KERNEL_FILE="${DEPLOY_DIR_IMAGE}/${KERNEL_IMAGETYPE}"
    RAMDISK_FILE="${DEPLOY_DIR_IMAGE}/$1"
    # Newer oe-core (>Krogoth) copies the image after this step.
    # Hence we need to use the path from ${IMGDEPLOYDIR}.
    if [ ! -e ${RAMDISK_FILE} ]; then
      RAMDISK_FILE="${IMGDEPLOYDIR}/$1"
    fi

    # Self-decompressing kernels (zImage, uImage, bzImage) go into the FIT as
    # is. A raw kernel such as the arm64 "Image" is compressed with
    # FIT_KERNEL_COMP_ALG, and U-Boot decompresses it at boot.
    KERNEL_COMPRESSION="none"
    case "${KERNEL_IMAGETYPE}" in
    zImage|uImage|bzImage)
        ;;
    *)
        KERNEL_COMPRESSION="${FIT_KERNEL_COMP_ALG}"
        ;;
    esac
    if [ "${KERNEL_COMPRESSION}" != "none" ]; then
        KERNEL_FILE_COMP="${WORKDIR}/fit-kernel.${KERNEL_COMPRESSION}"
        case "${KERNEL_COMPRESSION}" in
        gzip)
            gzip -9 -n -c "${KERNEL_FILE}" > "${KERNEL_FILE_COMP}"
            ;;
        lzma)
            lzma -9 -c "${KERNEL_FILE}" > "${KERNEL_FILE_COMP}"
            ;;
        *)
            bbfatal "FIT_KERNEL_COMP_ALG ${KERNEL_COMPRESSION} is not supported"
            ;;
        esac
        KERNEL_FILE="${KERNEL_FILE_COMP}"
    fi

    rm -f ${FIT_SOURCE}
    fitimage_emit_fit_header ${FIT_SOURCE}

    # Step 1: Prepare a kernel image section
    fitimage_emit_section_maint ${FIT_SOURCE} imagestart
    fitimage_emit_section_kernel ${FIT_SOURCE} 1 "${KERNEL_FILE}" "${KERNEL_COMPRESSION}" \
        ${UBOOT_LOADADDRESS} ${UBOOT_ENTRYPOINT}

    # Step 2: Prepare a ramdisk image section
    fitimage_emit_section_ramdisk ${FIT_SOURCE} 1 ${RAMDISK_FILE} "lzma"

    # Step 3: Prepare dtb image section (if needed)
    if test -n "${KERNEL_DEVICETREE}"; then
        dtbcount=1
        DFT_DTB_COUNTER=1
        for DTB in ${KERNEL_DEVICETREE}; do
            # All the aspeed openbmc dts files are moved to "aspeed"
            # directory under "arch/arm/boot/dts" since Linux v6.5, thus
            # "aspeed/" needs to be prefixed to the "KERNEL_DEVICETREE"
            # entries since Linux 6.5.
            # In order to support both legacy kernel and Linux 6.5 (or
            # higher versions), we extract the basename to ensure the
            # dtb files are located properly.
            if echo "${DTB}" | grep -q '/'; then
                bbnote "remove path prefix from ${DTB}"
                DTB=`basename ${DTB} | sed 's,\.dts$,.dtb,g'`
            fi
            DTB_PATH="${DEPLOY_DIR_IMAGE}/${DTB}"
            fitimage_emit_section_dtb ${FIT_SOURCE} ${dtbcount} ${DTB_PATH}
            dtbcount=`expr ${dtbcount} + 1`
        done
    else
        DFT_DTB_COUNTER=""
    fi
    fitimage_emit_section_maint ${FIT_SOURCE} sectend

    # Step 4: Prepare a configurations section
    fitimage_emit_section_maint ${FIT_SOURCE} confstart
    fitimage_emit_section_config ${FIT_SOURCE} 1 1 ${DFT_DTB_COUNTER}
    fitimage_emit_section_maint ${FIT_SOURCE} sectend
    fitimage_emit_section_maint ${FIT_SOURCE} fitend

    # Step 5: Assemble the image
    mkimage -f ${FIT_SOURCE} ${FIT_DESTINATION}
    ln -sf ${FIT} ${DEPLOY_DIR_IMAGE}/${FIT_LINK}
}
