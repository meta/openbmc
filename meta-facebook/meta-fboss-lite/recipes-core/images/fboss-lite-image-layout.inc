# The fixed meta-partition offset checked by fbobmc-image.bbclass must match
# the "image-meta" partition offset defined in the layout below. aspeed-g7
# (AST2700) uses a 4MB u-boot/manifest region, pushing image-meta to 0x00410000;
# other SoCs keep the 0x000F0000 default.
FBOBMC_IMAGE_META_OFFSET = "0x000F0000"
FBOBMC_IMAGE_META_OFFSET:aspeed-g7 = "0x00410000"

def fbobmc_get_image_part_table(d):
    # define the partition table of image with (offset, name, max_size)
    # the max_size is used to do sanity check of the partition define
    # it can be omitted as -1, which will be calculated automatically
    # the order in the partition in the list does not matter
    # sanity check will make sure the partitions not overlap, no gaps
    # align with erase_block_size and fits in the space defined as image_max_size
    image_max_size = 0x8000000
    erase_block_size = 64 * 1024

    # Based on Aspeed SDK_User_Guide_11.04.pdf page.54 and 58,
    # AST2700 A2 the size of u-boot is 4MB (manifest bundle)
    # and FIT package start address 0x00420000.

    soc_family = d.getVar('SOC_FAMILY') or ""

    if soc_family == 'aspeed-g7':
        image_part_table_simple = (
            #  offset,      name            max_size (K)  type              src_component ( 0: fill with zero, None: skip)
            (  0,           "u-boot",       -1          , "raw",        d.expand("${DEPLOY_DIR_IMAGE}/u-boot.${UBOOT_SUFFIX}") ),
            (  0x00400000,  "u-boot-env",   64          , "data",       0),
            (  0x00410000,  "image-meta",   64          , "meta",       0),
            (  0x00420000,  "os-fit",       -1          , "fit",        d.expand("${DEPLOY_DIR_IMAGE}/${FIT_LINK}")),
            (  0x04000000,  "data0",        64 * 1024   , "mtdonly",    None),
        )
    else:
        image_part_table_simple = (
            #  offset,      name            max_size (K)  type              src_component ( 0: fill with zero, None: skip)
            (  0,           "u-boot",       -1          , "raw",        d.expand("${DEPLOY_DIR_IMAGE}/u-boot-${MACHINE}.${UBOOT_SUFFIX}") ),
            (  0x000E0000,  "u-boot-env",   64          , "data",       0),
            (  0x000F0000,  "image-meta",   64          , "meta",       0),
            (  0x00100000,  "os-fit",       -1          , "fit",        d.expand("${DEPLOY_DIR_IMAGE}/${FIT_LINK}")),
            (  0x04000000,  "data0",        64 * 1024   , "mtdonly",    None),
        )

    part_table = list(image_part_table_simple)

    return (image_max_size, erase_block_size, part_table)
