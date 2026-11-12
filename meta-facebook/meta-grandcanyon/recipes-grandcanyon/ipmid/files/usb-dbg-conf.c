#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <openbmc/pal.h>
#include <openbmc/sdr.h>
#include <openbmc/obmc-i2c.h>
#include <openbmc/obmc_pal_sensors.h>
#include <facebook/bic.h>
#include <facebook/fbgc_common.h>
#include <syslog.h>
#include "usb-dbg-conf.h"

#define ESCAPE "\x1B"
#define ESC_ALT ESCAPE"[5;7m"
#define ESC_RST ESCAPE"[m"

#define MAX_UART_SEL_NAME        15
#define SIZE_OF_NULL_TERMINATOR  1

//These postcodes are defined in document "Barton Springs BIOS Specification Version 0.1"
static post_desc_t pdesc_phase1[] = {
  /*--------------------- SEC Phase - Start--------------------- */
  { 0x00, "NOT_USED" },
  { 0x01, "POWER_ON" },
  { 0x02, "MICROCODE" },
  { 0x03, "CACHE_ENABLED" },
  { 0x04, "SECSB_INIT" },
  { 0x06, "CPU_EARLY_INIT" },
  { 0xCA, "CHECK_GENUINE_CPU" },
  { 0xCB, "PUNI_VCU_FAIL" },
  { 0xCC, "ENABLE_CSR_FAIL" },
  { 0xCD, "LOAD_MICROC" },
  { 0xCE, "MMCFG_BUS_N_STACK" },
  { 0xD0, "CACHE_TEST_FAIL" },
  { 0xD1, "CONF_TEST_FAIL" },
  /*--------------------- SEC Phase - End----------------------- */

  /*--------------------- PEI Phase - Start--------------------- */
  { 0x10, "PEI_CORE_START" },
  { 0x11, "PM_CPU_INITS" },
  { 0x12, "PM_CPU_INIT1" },
  { 0x13, "PM_CPU_INIT2" },
  { 0x14, "PM_CPU_INIT3" },
  { 0x15, "PM_NB_INITS" },
  { 0x16, "PM_NB_INIT1" },
  { 0x17, "PM_NB_INIT2" },
  { 0x18, "PM_NB_INIT3" },
  { 0x19, "PM_SB_INITS" },
  { 0x1A, "PM_SB_INIT1" },
  { 0x1B, "PM_SB_INIT2" },
  { 0x1C, "PM_SB_INIT3" },
  { 0x2B, "MEM_INIT_SPD" },
  { 0x2C, "MEM_INIT_PRS" },
  { 0x2D, "MEM_INIT_PROG" },
  { 0x2E, "MEM_INIT_CFG" },
  { 0x2F, "MEM_INIT" },
  { 0x31, "MEM_INSTALLED" },
  { 0x32, "CPU_INITS" },
  { 0x33, "CPU_CACHE_INIT" },
  { 0x34, "CPU_AP_INIT" },
  { 0x35, "CPU_BSP_INIT" },
  { 0x36, "CPU_SMM_INIT" },
  { 0x37, "NB_INITS" },
  { 0x38, "NB_INIT1" },
  { 0x39, "NB_INIT2" },
  { 0x3A, "NB_INIT3" },
  { 0x3B, "SB_INITS" },
  { 0x3C, "SB_INIT1" },
  { 0x3D, "SB_INIT2" },
  { 0x3E, "SB_INIT3" },
  { 0x4F, "DXE_IPL_START" },
  
  // KTI POST CODE
  { 0xA0, "STS_DATA_INIT" },
  { 0xA1, "STS_COLLECT_INFO" },
  { 0xA3, "STS_SETUP_PATH" },
  { 0xA6, "STS_PBSP_SYNC" },
  { 0xA7, "STS_TOPOLOGY_DISC" },
  { 0xA8, "STS_FINAL_ROUTE" },
  { 0xA9, "STS_FINAL_IO_SAD" },
  { 0xAA, "STS_PROTOCOL_SET" },
  { 0xAB, "STS_FULL_SPEED" },
  { 0xAE, "STS_COHERNCY_SETUP" },
  { 0xAF, "STS_KTI_COMPLETE" },
  
  // KTI ERROR CODE
  { 0xD8, "BOOT_MODE_ERR" },
  { 0xD9, "MIN_PATH_SETUP_ERR" },
  { 0xDA, "TOPOLOGY_DISC_ERR" },
  { 0xDB, "SAD_SETUP_ERR" },
  { 0xDC, "UNSUP_TOPOLOGY_ERR" },
  { 0xDD, "FULL_SPEED_ERR" },
  { 0xDE, "S3_RESUME_ERR" },
  { 0xDF, "SOFT_CHECK_ERR" },
  
  // IIO INIT POST CODE
  { 0xE0, "STS_EARLY_INIT" },
  { 0xE1, "STS_PRE_LINK_TRAIN" },
  { 0xE2, "STS_GEN3_EQ_PROM" },
  { 0xE3, "STS_LINK_TRAIN" },
  { 0xE4, "STS_GEN3_OVERRIDE" },
  { 0xE5, "STS_EARLY_INIT_END" },
  { 0xE6, "STS_LATE_INIT" },
  { 0xE7, "STS_PCIE_PORT_INIT" },
  { 0xE8, "STS_IOAPIC_INIT" },
  { 0xE9, "STS_VTD_INIT" },
  { 0xEA, "STS_IOAT_INIT" },
  { 0xEB, "STS_DFX_INIT" },
  { 0xEC, "STS_NTB_INIT" },
  { 0xED, "STS_SEC_INIT" },
  { 0xEE, "STS_LATE_INIT_END" },
  { 0xEF, "STS_READY_TO_BOOT" },
  { 0x50, "INVALID_MEM" },
  { 0x51, "SPD_READ_FAIL" },
  { 0x52, "INVALID_MEM_SIZE" },
  { 0x53, "NO_USABLE_MEMORY" },
  { 0x54, "MEM_INIT_ERROR" },
  { 0x55, "MEM_NOT_INSTALLED" },
  { 0x56, "INVALID_CPU" },
  { 0x57, "CPU_MISMATCH" },
  { 0x58, "CPU_SELFTEST_FAIL" },
  { 0x59, "MICROCODE_FAIL" },
  { 0x5A, "CPU_INT_ERR" },
  { 0x5B, "RESET_PPI_ERR" },

  // S3 Resume Progress Code
  { 0xE0, "S3_RESUME_STARTED" },
  { 0xE1, "S3_BOOT_SCRIPT_EXE" },
  { 0xE2, "VIDEO_REPOST" },
  { 0xE3, "S3_WAKE_VECTOR_CALL" },
  
  // S3 Resume Error Code
  { 0xE8, "S3_RESUME_FAIL" },
  { 0xE9, "S3_PPI_NOT_FOUND" },
  { 0xEA, "S3_BOOT_SCRIPT_ERR" },
  { 0xEB, "S3_OS_WAKE_ERR" },
  
  // Recovery Progress Code
  { 0xF0, "REC_BY_FW" },
  { 0xF1, "REC_BY_USER" },
  { 0xF2, "REC_STARTED" },
  { 0xF3, "REC_FW_FOUNDED" },
  { 0xF4, "REC_FW_LOADED" },
  
  // Recovery Error code
  { 0xF8, "RECOVERY_PPI_FAIL" },
  { 0xF9, "RECOVERY_CAP_NFOUND" },
  { 0xFA, "RECOVERY_CAP_ERR" },
  
  /*--------------------- PEI Phase - End----------------------- */
};

static post_desc_t pdesc_phase2[] = {
  /*--------------------- DXE Phase - Start--------------------- */
  { 0x60, "DXE_CORE_START" },
  { 0x61, "NVRAM_INIT" },
  { 0x62, "INSTALL_SB_SERVICE" },
  { 0x63, "CPU_DXE_STARTED" },
  { 0x64, "CPU_DXE_INIT" },
  { 0x65, "CPU_DXE_INIT" },
  { 0x66, "CPU_DXE_INIT" },
  { 0x67, "CPU_DXE_INIT" },
  { 0x68, "PCI_BRIDEGE_INIT" },
  { 0x69, "NB_DEX_INIT" },
  { 0x6A, "NB_DEX_SMM_INIT" },
  { 0x6B, "NB_DEX_BRI_START" },
  { 0x6C, "NB_DEX_BRI_START" },
  { 0x6D, "NB_DEX_BRI_START" },
  { 0x6E, "NB_DEX_BRI_START" },
  { 0x6F, "NB_DEX_BRI_START" },
  { 0x70, "SB_DXE_START" },
  { 0x71, "SB_DEX_SMM_INIT" },
  { 0x72, "SB_DEX_DEV_START" },
  { 0x73, "SB_DEX_BRI_START" },
  { 0x74, "SB_DEX_BRI_START" },
  { 0x75, "SB_DEX_BRI_START" },
  { 0x76, "SB_DEX_BRI_START" },
  { 0x77, "SB_DEX_BRI_START" },
  { 0x78, "ACPI_INIT" },
  { 0x79, "CSM_INIT" },
  { 0x7A, "AMI_DXE_CODE" },
  { 0x7B, "AMI_DXE_CODE" },
  { 0x7C, "AMI_DXE_CODE" },
  { 0x7D, "AMI_DXE_CODE" },
  { 0x7E, "AMI_DXE_CODE" },
  { 0x7F, "AMI_DXE_CODE" },
  { 0x80, "OEM_DEX_INIT" },
  { 0x81, "OEM_DEX_INIT" },
  { 0x82, "OEM_DEX_INIT" },
  { 0x83, "OEM_DEX_INIT" },
  { 0x84, "OEM_DEX_INIT" },
  { 0x85, "OEM_DEX_INIT" },
  { 0x86, "OEM_DEX_INIT" },
  { 0x87, "OEM_DEX_INIT" },
  { 0x88, "OEM_DEX_INIT" },
  { 0x89, "OEM_DEX_INIT" },
  { 0x8A, "OEM_DEX_INIT" },
  { 0x8B, "OEM_DEX_INIT" },
  { 0x8C, "OEM_DEX_INIT" },
  { 0x8D, "OEM_DEX_INIT" },
  { 0x8E, "OEM_DEX_INIT" },
  { 0x8F, "OEM_DEX_INIT" },
  { 0x90, "BDS_START" },
  { 0x91, "DRIVER_CONN_START" },
  { 0x92, "PCIB_INIT" },
  { 0x93, "PCIB_HOT_PLUG_INIT" },
  { 0x94, "PCIB_ENUMERATION" },
  { 0x95, "PCIB_REQ_RESOURCE" },
  { 0x96, "PCIB_ASS_RESOURCE" },
  { 0x97, "CONSOLE_OUTPUT_CONN" },
  { 0x98, "CONSOLE_INPUT_CONN" },
  { 0x99, "SUPER_IO_INIT" },
  { 0x9A, "USB_INIT" },
  { 0x9B, "USB_RESET" },
  { 0x9C, "USB_DETECT" },
  { 0x9D, "USB_ENABLE" },
  { 0x9E, "AMI_CODE" },
  { 0x9F, "AMI_CODE" },
  { 0xA0, "IDE_INIT" },
  { 0xA1, "IDE_RESET" },
  { 0xA2, "IDE_DETECT" },
  { 0xA3, "IDE_ENABLE" },
  { 0xA4, "SCSI_INIT" },
  { 0xA5, "SCSI_RESET" },
  { 0xA6, "SCSI_DETECT" },
  { 0xA7, "SCSI_ENABLE" },
  { 0xA8, "SETUP_VERIFY_PW" },
  { 0xA9, "START_OF_SETUP" },
  { 0xAB, "SETUP_INPUT_WAIT" },
  { 0xAD, "READY_TO_BOOT" },
  { 0xAE, "LEGACY_BOOT_EVE" },
  { 0xAF, "EXIT_BOOT_EVE" },
  { 0xB0, "SET_VIR_ADDR_START" },
  { 0xB1, "SET_VIR_ADDR_END" },
  { 0xB2, "LEGACY_ROM_INIT" },
  { 0xB3, "SYS_RESET" },
  { 0xB4, "USB_HOT_PLUG" },
  { 0xB5, "PCIB_HOT_PLUG" },
  { 0xB6, "CLEAN_NVRAM" },
  { 0xB7, "CONFIG_RESET" },
  { 0xB8, "AMI_CODE" },
  { 0xB9, "AMI_CODE" },
  { 0xBA, "ASL_CODE" },
  { 0xBC, "AMI_CODE" },
  { 0xBD, "AMI_CODE" },
  { 0xBE, "AMI_CODE" },
  { 0xBF, "AMI_CODE" },
  
  // OEM POST CODE
  { 0xC0, "OEM_DXE_OPROM_START" },
  { 0xC1, "OEM_DXE_OPROM_END" },

  // ERROR CODES
  { 0x58, "CPU_SELFTEST_FAIL" },
  { 0xD0, "CPU_INIT_ERR" },
  { 0xD1, "NB_INIT_ERR" },
  { 0xD2, "SB_INIT_ERR" },
  { 0xD3, "ARCH_PROT_ERR" },
  { 0xD4, "PCI_ALLOC_ERR" },
  { 0xD5, "NO_SPACE_ROM" },
  { 0xD6, "NO_CONSOLE_OUT" },
  { 0xD7, "NO_CONSOLE_IN" },
  { 0xD8, "INVALID_PW" },
  { 0xD9, "BOOT_OPT_ERR" },
  { 0xDA, "BOOT_OPT_FAIL" },
  { 0xDB, "FLASH_UPD_FAIL" },
  { 0xDC, "RST_PROT_NA" },

  // BDS PHASE
  { 0x10, "SIG_DRVERS_EVT" },
  { 0x11, "SIG_COMP_PMP_EVT" },
  { 0x12, "CONN_ROOT_BRI_HAN" },
  { 0x13, "REG_MEM_INFO_CB" },
  { 0x14, "REPORT_CON_PC" },
  { 0x15, "CONN_VGA_OUT" },
  { 0x16, "CONN_OUT_VAR" },
  { 0x17, "INS_CON_OUT_S" },
  { 0x18, "REPORT_CONSOLE_CODE" },
  { 0x19, "CONN_PS2_IN" },
  { 0x1A, "CONN_USB_IN" },
  { 0x1B, "CONN_CONSOLE_VAR" },
  { 0x1C, "INS_CONSOLE" },
  { 0x1D, "CONSOLE_IN_AVAIL_BEEP" },
  { 0x1E, "CONN_EVERY" },
  { 0x1F, "RUN_DRIVERS" },
  { 0x20, "INIT_CONSOLE_VAR" },
  { 0x21, "IDE_CONN_CONTROL" },
  { 0x22, "SHOW_LEGACY_OPT_ROM" },
  { 0x23, "SIG_ALL_DRV_CONN_EVT" },
  { 0x24, "SIG_EXIT_PM_AUTH_EVT" },
  { 0x25, "INS_FW_LOAD_FILE" },
  { 0x26, "UPD_BOOT_OPT_VAR" },
  { 0x27, "RESTORE_BOOT_ORDER" },
  { 0x28, "ADJ_EFI_OS_BOOT_ORDER" },
  { 0x29, "READ_BOOT_OPTION" },
  { 0x2A, "UNMASK_ORPHAN_DEV" },
  { 0x2B, "COLLECT_USB_BDS_DEV" },
  { 0x2C, "COLLECT_BOOT_DEV" },
  { 0x2D, "FILTER_BOOT_DEV" },
  { 0x2E, "CREATE_EFI_BOOT_OPT" },
  { 0x2F, "MATCH_BOOT_OPT" },
  { 0x30, "DEL_UNMATCH_UEFI_HD" },
  { 0x31, "CREATE_BOOT_OPT_NEW" },
  { 0x32, "SET_BOOT_OPT_TAGS" },
  { 0x33, "EFI_OS_NAME_NORMALIZE" },
  { 0x34, "PRE_PROC_BOOT_OPT" },
  { 0x35, "SET_BOOT_OPT_PRIORITY" },
  { 0x36, "SET_IPMI_BOOT_OPT_PRI" },
  { 0x37, "ADJ_UEFI_OS_OPT_PRI" },
  { 0x38, "POST_PROC_BOOT_OPT" },
  { 0x39, "MASK_ORPHAN_DEV" },
  { 0x3A, "SAVE_LEGACY_BOOT_ORDER" },
  { 0x3B, "SAVE_BOOT_OPT" },
  { 0x3C, "CALL_DISPATCHER" },
  { 0x3D, "RECOVER_MEM_ABOVE_4G" },
  { 0x3E, "CAPSULE_VAR_CLEANUP" },
  { 0x3F, "HAND_OFF_TO_TSE" },
  /*--------------------- DXE Phase - End--------------------- */
};

static post_phase_desc_t post_phase_desc[] = {
  {1, pdesc_phase1, sizeof(pdesc_phase1)/sizeof(pdesc_phase1[0])},
  {2, pdesc_phase2, sizeof(pdesc_phase2)/sizeof(pdesc_phase2[0])},
};

// {pin,level,def,desc}
static dbg_gpio_desc_t gdesc[] = {
  { 0x10, 0, 2, "DEBUG_RST_BTN_N" },
  { 0x11, 0, 1, "DEBUG_PWR_BTN_N" },
  { 0x12, 0, 0, "DEBUG_GPIO_BMC_2" },
  { 0x13, 0, 0, "DEBUG_GPIO_BMC_3" },
  { 0x14, 0, 0, "DEBUG_GPIO_BMC_4" },
  { 0x15, 0, 0, "DEBUG_GPIO_BMC_5" },
  { 0x16, 0, 0, "DEBUG_GPIO_BMC_6" },
  { 0x17, 0, 3, "DEBUG_BMC_UART_SEL_R" },
};
#ifdef CONFIG_GRANDCANYON2
// {name, sensor_num, unit, fru, disp_prec}
static sensor_desc_t cri_sensor[] =
{
    {"SYS_HSC_PWR:" , PTB_P48V_AUX_Power         , "W"   , FRU_DPB, 1},
    {"SYS_HSC_VOL:" , PTB_P48V_AUX               , "V"   , FRU_DPB, 2},
    {"SYS_HSC_CUR:" , PTB_P48V_AUX_Current       , "A"   , FRU_DPB, 2},
    {"FAN0_F:"      , FAN_0_FRONT                , "RPM" , FRU_DPB, 0},
    {"FAN0_R:"      , FAN_0_REAR                 , "RPM" , FRU_DPB, 0},
    {"FAN1_F:"      , FAN_1_FRONT                , "RPM" , FRU_DPB, 0},
    {"FAN1_R:"      , FAN_1_REAR                 , "RPM" , FRU_DPB, 0},
    {"FAN2_F:"      , FAN_2_FRONT                , "RPM" , FRU_DPB, 0},
    {"FAN2_R:"      , FAN_2_REAR                 , "RPM" , FRU_DPB, 0},
    {"FAN3_F:"      , FAN_3_FRONT                , "RPM" , FRU_DPB, 0},
    {"FAN3_R:"      , FAN_3_REAR                 , "RPM" , FRU_DPB, 0},
    {"NIC Temp:"    , NIC_SENSOR_TEMP            , "C"   , FRU_NIC, 0},
};
#else
// {name, sensor_num, unit, fru, disp_prec}
static sensor_desc_t cri_sensor[] =
{
    {"CPU_TEMP:"    , BS_CPU_TEMP                , "C"   , FRU_SERVER, 0},
    {"DIMM0_TEMP:"  , BS_DIMM0_TEMP              , "C"   , FRU_SERVER, 0},
    {"DIMM1_TEMP:"  , BS_DIMM1_TEMP              , "C"   , FRU_SERVER, 0},
    {"DIMM2_TEMP:"  , BS_DIMM2_TEMP              , "C"   , FRU_SERVER, 0},
    {"DIMM4_TEMP:"  , BS_DIMM3_TEMP              , "C"   , FRU_SERVER, 0},
    {"SYS_HSC_PWR:" , DPB_HSC_PWR_CLIP           , "W"   , FRU_DPB, 1},
    {"SYS_HSC_VOL:" , DPB_HSC_P12V_CLIP          , "V"   , FRU_DPB, 2},
    {"SYS_HSC_CUR:" , DPB_HSC_CUR_CLIP           , "A"   , FRU_DPB, 2},
    {"FAN0_F:"      , FAN_0_FRONT                , "RPM" , FRU_DPB, 0},
    {"FAN0_R:"      , FAN_0_REAR                 , "RPM" , FRU_DPB, 0},
    {"FAN1_F:"      , FAN_1_FRONT                , "RPM" , FRU_DPB, 0},
    {"FAN1_R:"      , FAN_1_REAR                 , "RPM" , FRU_DPB, 0},
    {"FAN2_F:"      , FAN_2_FRONT                , "RPM" , FRU_DPB, 0},
    {"FAN2_R:"      , FAN_2_REAR                 , "RPM" , FRU_DPB, 0},
    {"FAN3_F:"      , FAN_3_FRONT                , "RPM" , FRU_DPB, 0},
    {"FAN3_R:"      , FAN_3_REAR                 , "RPM" , FRU_DPB, 0},
    {"NIC Temp:"    , NIC_SENSOR_TEMP            , "C"   , FRU_NIC, 0},
};
#endif


// Expander error code format:
//   GC1: decimal-style code (e.g. 0x99 represents 99)
//   GC2: hexadecimal code
// BMC error codes are hexadecimal.
// The Debug Card returns the same code to BMC for error string lookup.

// Expander error codes are defined in the following documents:
//   GC1.0: "GrandCanyon_ErrorCode_EventLogList_0312"
//   GC2.0: "GC2_errorcode_index"
static post_desc_t pdesc_error[] = {
  {0, "No error"},
#ifdef CONFIG_GRANDCANYON2
  {0x01, "SCC_EXP/IOC_*V*_VOLT_V crit"},
  {0x02, "SCC_HSC/EFUSE_12V_VOLT_V crit"},
  {0x03, "DPB_*V*_*/STBY/IN_VOLT_V crit"},
  {0x04, "DPB_EFUSE_12V_VOLT_V crit"},
  {0x05, "FAN_EFUSE0_12V_VOLT_V crit"},
  {0x06, "FAN_EFUSE1_12V_VOLT_V crit"},
  {0x07, "FAN_EFUSE2_12V_VOLT_V crit"},
  {0x08, "FAN_EFUSE3_12V_VOLT_V crit"},
  {0x09, "PTB_HSC_48V_AUX_VOLT_V crit"},
  {0x0A, "PTB_DCMODULE0_12V_VOLT_V crit"},
  {0x0B, "PTB_DCMODULE1_12V_VOLT_V crit"},
  {0x0C, "PTB_VSENSE_48V_VOLT_V crit"},
  {0x0D, "PTB_VSENSE_GND_VOLT_V crit"},
  {0x0E, "PTB_TSENSE_48V_VOLT_V crit"},
  {0x0F, "PTB_TSENSE_GND_VOLT_V crit"},
  {0x10, "DPB_HDD*_5V/12V_VOLT_V crit"},
  {0x11, "Reserved"},
  {0x12, "Reserved"},
  {0x13, "Reserved"},
  {0x14, "Reserved"},
  {0x15, "Reserved"},
  {0x16, "Reserved"},
  {0x17, "Reserved"},
  {0x18, "Reserved"},
  {0x19, "Reserved"},
  {0x1A, "Reserved"},
  {0x1B, "Reserved"},
  {0x1C, "Reserved"},
  {0x1D, "Reserved"},
  {0x1E, "Reserved"},
  {0x1F, "Reserved"},
  {0x20, "Reserved"},
  {0x21, "HDD0 fault request"},
  {0x22, "HDD1 fault request"},
  {0x23, "HDD2 fault request"},
  {0x24, "HDD3 fault request"},
  {0x25, "HDD4 fault request"},
  {0x26, "HDD5 fault request"},
  {0x27, "HDD6 fault request"},
  {0x28, "HDD7 fault request"},
  {0x29, "HDD8 fault request"},
  {0x2A, "HDD9 fault request"},
  {0x2B, "HDD10 fault request"},
  {0x2C, "HDD11 fault request"},
  {0x2D, "HDD12 fault request"},
  {0x2E, "HDD13 fault request"},
  {0x2F, "HDD14 fault request"},
  {0x30, "HDD15 fault request"},
  {0x31, "HDD16 fault request"},
  {0x32, "HDD17 fault request"},
  {0x33, "HDD18 fault request"},
  {0x34, "HDD19 fault request"},
  {0x35, "HDD20 fault request"},
  {0x36, "HDD21 fault request"},
  {0x37, "HDD22 fault request"},
  {0x38, "HDD23 fault request"},
  {0x39, "HDD24 fault request"},
  {0x3A, "HDD25 fault request"},
  {0x3B, "HDD26 fault request"},
  {0x3C, "HDD27 fault request"},
  {0x3D, "HDD28 fault request"},
  {0x3E, "HDD29 fault request"},
  {0x3F, "HDD30 fault request"},
  {0x40, "HDD31 fault request"},
  {0x41, "HDD32 fault request"},
  {0x42, "HDD33 fault request"},
  {0x43, "HDD34 fault request"},
  {0x44, "HDD35 fault request"},
  {0x45, "HDD fault sensed"},
  {0x46, "Reserved"},
  {0x47, "Reserved"},
  {0x48, "Internal mini-SAS Link Loss"},
  {0x49, "Internal SAS Link Loss"},
  {0x4A, "SCC I2C device loss"},
  {0x4B, "DPB I2C device loss"},
  {0x4C, "PTB I2C device loss"},
  {0x4D, "UIC I2C device loss"},
  {0x4E, "Reserved"},
  {0x4F, "Fan0 Front fault"},
  {0x50, "Fan0 Rear fault"},
  {0x51, "Fan1 Front fault"},
  {0x52, "Fan1 Rear fault"},
  {0x53, "Fan2 Front fault"},
  {0x54, "Fan2 Rear fault"},
  {0x55, "Fan3 Front fault"},
  {0x56, "Fan3 Rear fault"},
  {0x57, "Reserved"},
  {0x58, "Reserved"},
  {0x59, "Reserved"},
  {0x5A, "Drawer Pull out"},
  {0x5B, "Peer SCC Plug out"},
  {0x5C, "UICA Plug out"},
  {0x5D, "UICB Plug out"},
  {0x5E, "Fan 0 Plug out"},
  {0x5F, "Fan 1 Plug out"},
  {0x60, "Fan 2 Plug out"},
  {0x61, "Fan 3 Plug out"},
  {0x62, "Reserved"},
  {0x63, "P3V3_IN PG low"},
  {0x64, "SCC_EFUSE_12V PG low"},
  {0x65, "DPB_EFUSE_12V PG low"},
  {0x66, "FAN_EFUSE0_12V PG low"},
  {0x67, "FAN_EFUSE1_12V PG low"},
  {0x68, "FAN_EFUSE2_12V PG low"},
  {0x69, "FAN_EFUSE3_12V PG low"},
  {0x6A, "FAN_EFUSE0_12V Fault trigger"},
  {0x6B, "FAN_EFUSE1_12V Fault trigger"},
  {0x6C, "FAN_EFUSE2_12V Fault trigger"},
  {0x6D, "FAN_EFUSE3_12V Fault trigger"},
  {0x6E, "FAN_EFUSE0_12V Alert trigger"},
  {0x6F, "FAN_EFUSE1_12V Alert trigger"},
  {0x70, "FAN_EFUSE2_12V Alert trigger"},
  {0x71, "FAN_EFUSE3_12V Alert trigger"},
  {0x72, "Reserved"},
  {0x73, "Reserved"},
  {0x74, "Reserved"},
  {0x75, "Reserved"},
  {0x76, "Reserved"},
  {0x77, "SCC_EXP_TEMP_C crit"},
  {0x78, "SCC_BOTTOM_TEMP_C crit"},
  {0x79, "SCC_TOP_TEMP_C crit"},
  {0x7A, "SCC_EFUSE_12V_TEMP_C crit"},
  {0x7B, "DPB_0_INLET_TEMP_C crit"},
  {0x7C, "DPB_1_INLET_TEMP_C crit"},
  {0x7D, "DPB_OUTLET_TEMP_C crit"},
  {0x7E, "DPB_EFUSE_12V_TEMP_C crit"},
  {0x7F, "FAN_EFUSE0_12V_TEMP_C crit"},
  {0x80, "FAN_EFUSE1_12V_TEMP_C crit"},
  {0x81, "FAN_EFUSE2_12V_TEMP_C crit"},
  {0x82, "FAN_EFUSE3_12V_TEMP_C crit"},
  {0x83, "PTB_DCMODULE0_12V_TEMP_C crit"},
  {0x84, "PTB_DCMODULE1_12V_TEMP_C crit"},
  {0x85, "HDD*_TEMP_C crit"},
  {0x86, "UIC_TEMP_C crit"},
  {0x87, "PTB_HSC_48V_AUX_TEMP_C crit"},
  {0x88, "Reserved"},
  {0x89, "Reserved"},
  {0x8A, "Reserved"},
  {0x8B, "Reserved"},
  {0x8C, "Reserved"},
  {0x8D, "Reserved"},
  {0x8E, "SCC_HSC/EFUSE_12V_CURR_A crit"},
  {0x8F, "DPB_HSC/EFUSE_12V_CURR_A crit"},
  {0x90, "FAN_EFUSE0_12V_CURR_A crit"},
  {0x91, "FAN_EFUSE1_12V_CURR_A crit"},
  {0x92, "FAN_EFUSE2_12V_CURR_A crit"},
  {0x93, "FAN_EFUSE3_12V_CURR_A crit"},
  {0x94, "PTB_HSC_48V_AUX_CURR_A crit"},
  {0x95, "PTB_DCMODULE0_12V_CURR_A crit"},
  {0x96, "PTB_DCMODULE1_12V_CURR_A crit"},
  {0x97, "Reserved"},
  {0x98, "Reserved"},
  {0x99, "Reserved"},
  {0x9A, "Reserved"},
  {0x9B, "Reserved"},
  {0x9C, "Reserved"},
  {0x9D, "Reserved"},
  {0x9E, "Reserved"},
  {0x9F, "Reserved"},
  {0xA0, "Reserved"},
  {0xA1, "Reserved"},
  {0xA2, "Reserved"},
  {0xA3, "Reserved"},
  {0xA4, "Reserved"},
  {0xA5, "Reserved"},
  {0xA6, "Reserved"},
  {0xA7, "Reserved"},
  {0xA8, "Reserved"},
  {0xA9, "Reserved"},
  {0xAA, "Reserved"},
  {0xAB, "I2C Bus ID 1 Crash"},
  {0xAC, "I2C Bus ID 2 Crash"},
  {0xAD, "I2C Bus ID 3 Crash"},
  {0xAE, "I2C Bus ID 4 Crash"},
  {0xAF, "I2C Bus ID 5 Crash"},
  {0xB0, "I2C Bus ID 6 Crash"},
  {0xB1, "I2C Bus ID 7 Crash"},
  {0xB2, "I2C Bus ID 0 Crash"},
  {0xB3, "IOC HeartBeat loss"},
  {0xB4, "Reserved"},
  {0xB5, "Reserved"},
  {0xB6, "Reserved"},
  {0xB7, "Reserved"},
  {0xB8, "Reserved"},
  {0xB9, "Reserved"},
  {0xBA, "Reserved"},
  {0xBB, "Reserved"},
  {0xBC, "Reserved"},
  {0xBD, "Reserved"},
  {0xBE, "Reserved"},
  {0xBF, "Reserved"},
  {0xC0, "Reserved"},
  {0xC1, "Reserved"},
  {0xC2, "Reserved"},
  {0xC3, "Reserved"},
  {0xC4, "Reserved"},
  {0xC5, "Reserved"},
  {0xC6, "Reserved"},
  {0xC7, "Reserved"},
  {0xC8, "Reserved"},
  {0xC9, "Reserved"},
  {0xCA, "Reserved"},
  {0xCB, "Reserved"},
  {0xCC, "Reserved"},
  {0xCD, "Reserved"},
  {0xCE, "Reserved"},
  {0xCF, "Reserved"},
  {0xD0, "Reserved"},
  {0xD1, "Reserved"},
  {0xD2, "Reserved"},
  {0xD3, "Reserved"},
  {0xD4, "Reserved"},
  {0xD5, "Reserved"},
  {0xD6, "Reserved"},
  {0xD7, "Reserved"},
  {0xD8, "Reserved"},
  {0xD9, "Reserved"},
  {0xDA, "Reserved"},
  {0xDB, "Reserved"},
  {0xDC, "Reserved"},
  {0xDD, "Reserved"},
  {0xDE, "Reserved"},
  {0xDF, "Reserved"},
  {0xE0, "Reserved"},
  {0xE1, "Reserved"},
  {0xE2, "Reserved"},
  {0xE3, "Reserved"},
  {0xE4, "Reserved"},
  {0xE5, "Reserved"},
  {0xE6, "Reserved"},
  {0xE7, "Reserved"},
  {0xE8, "Reserved"},
  {0xE9, "Reserved"},
  {0xEA, "Reserved"},
  {0xEB, "Reserved"},
  {0xEC, "Reserved"},
  {0xED, "Reserved"},
  {0xEE, "Reserved"},
  {0xEF, "Reserved"},
  {0xF0, "Reserved"},
  {0xF1, "Reserved"},
  {0xF2, "Reserved"},
  {0xF3, "Reserved"},
  {0xF4, "Reserved"},
  {0xF5, "Reserved"},
  {0xF6, "Reserved"},
  {0xF7, "Reserved"},
  {0xF8, "Reserved"},
  {0xF9, "Reserved"},
  {0xFA, "Reserved"},
  {0xFB, "Reserved"},
  {0xFC, "Reserved"},
  {0xFD, "Reserved"},
  {0xFE, "Sync Exp Error Code Fault"},
  {0xFF, "Configuration/Type Not Match"},
#else
  {0x1, "Expander I2C bus 1 crash"},
  {0x2, "Expander I2C bus 2 crash"},
  {0x3, "Expander I2C bus 3 crash"},
  {0x4, "Expander I2C bus 4 crash"},
  {0x5, "Expander I2C bus 5 crash"},
  {0x6, "Expander I2C bus 6 crash"},
  {0x7, "Expander I2C bus 7 crash"},
  {0x8, "Expander I2C bus 0 crash"},
  {0x11, "SCC voltage critical"},
  {0x12, "SCC_HSC voltage critical"},
  {0x13, "DPB voltage critical"},
  {0x14, "DPB_HSC voltage critical"},
  {0x15, "PTB_HSC_CLIP voltage critical"},
  {0x16, "HDD X voltage critical"},
  {0x17, "SCC current critical"},
  {0x18, "DPB current critical"},
  {0x19, "PTB_HSC_CLIP current critical"},
  {0x21, "SCC_Expander_Temp critical"},
  {0x23, "SCC_Temp1 critical"},
  {0x24, "SCC_Temp2 critical"},
  {0x25, "DPB_INLET_Temp1 critical"},
  {0x26, "DPB_INLET_Temp2 critical"},
  {0x27, "DPB_OUTLET_Temp critical"},
  {0x28, "HDD X SMART Temp critical"},
  {0x29, "UIC_Temp critical"},
  {0x30, "HDD0 fault"},
  {0x31, "HDD1 fault"},
  {0x32, "HDD2 fault"},
  {0x33, "HDD3 fault"},
  {0x34, "HDD4 fault"},
  {0x35, "HDD5 fault"},
  {0x36, "HDD6 fault"},
  {0x37, "HDD7 fault"},
  {0x38, "HDD8 fault"},
  {0x39, "HDD9 fault"},
  {0x40, "HDD10 fault"},
  {0x41, "HDD11 fault"},
  {0x42, "HDD12 fault"},
  {0x43, "HDD13 fault"},
  {0x44, "HDD14 fault"},
  {0x45, "HDD15 fault"},
  {0x46, "HDD16 fault"},
  {0x47, "HDD17 fault"},
  {0x48, "HDD18 fault"},
  {0x49, "HDD19 fault"},
  {0x50, "HDD20 fault"},
  {0x51, "HDD21 fault"},
  {0x52, "HDD22 fault"},
  {0x53, "HDD23 fault"},
  {0x54, "HDD24 fault"},
  {0x55, "HDD25 fault"},
  {0x56, "HDD26 fault"},
  {0x57, "HDD27 fault"},
  {0x58, "HDD28 fault"},
  {0x59, "HDD29 fault"},
  {0x60, "HDD30 fault"},
  {0x61, "HDD31 fault"},
  {0x62, "HDD32 fault"},
  {0x63, "HDD33 fault"},
  {0x64, "HDD34 fault"},
  {0x65, "HDD35 fault"},
  {0x66, "HDD X fault sensed"},
  {0x70, "Internal Mini-SAS Link Loss"},
  {0x71, "Internal SAS Link Loss"},
  {0x72, "SCC I2C device loss"},
  {0x73, "DPB I2C device loss"},
  {0x74, "PTB I2C device loss"},
  {0x75, "UIC I2C device loss"},
  {0x77, "Fan 0 front fault"},
  {0x78, "Fan 0 rear fault"},
  {0x79, "Fan 1 front fault"},
  {0x80, "Fan 1 rear fault"},
  {0x81, "Fan 2 front fault"},
  {0x82, "Fan 2 rear fault"},
  {0x83, "Fan 3 front fault"},
  {0x84, "Fan 3 rear fault"},
  {0x88, "Drawer Pull out"},
  {0x89, "Peer SCC Plug out"},
  {0x90, "UICA Plug out"},
  {0x91, "UICB Plug out"},
  {0x92, "Fan 0 Plug out"},
  {0x93, "Fan 1 Plug out"},
  {0x94, "Fan 2 Plug out"},
  {0x95, "Fan 3 Plug out"},
  {0x99, "HW Configuration_Type Not Match"},
  {0xFE, "Failed to Get Expander Error Code"},
#endif
};

static post_phase_desc_t error_phase_desc[] = {
  {PHASE_ANY, pdesc_error, sizeof(pdesc_error)/sizeof(pdesc_error[0])}
};

bool plat_supported(void)
{
  return true;
}

int plat_get_post_phase(uint8_t fru, post_phase_desc_t **desc, size_t *desc_count)
{
  uint8_t uart_sel = 0;

  if (desc == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: desc is NULL\n", __func__);
    return -1;
  }
  if (desc_count == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: desc_count is NULL\n", __func__);
    return -1;
  }

  if (pal_get_debug_card_uart_sel(&uart_sel) < 0) {
    syslog(LOG_WARNING, " %s Fail to get debug cart uart selection number\n", __func__);
    return -1;
  }

  if (uart_sel == DEBUG_UART_SEL_BMC) {
    *desc = error_phase_desc;
    *desc_count = ARRAY_SIZE(error_phase_desc);

  } else {
    *desc = post_phase_desc;
    *desc_count = ARRAY_SIZE(post_phase_desc);
  }

  return 0;
}

int plat_get_gdesc(uint8_t fru, dbg_gpio_desc_t **desc, size_t *desc_count)
{
  if (desc == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: desc is NULL\n", __func__);
    return -1;
  }
  if (desc_count == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: desc_count is NULL\n", __func__);
    return -1;
  }

  *desc = gdesc;
  *desc_count = ARRAY_SIZE(gdesc);

  return 0;
}

int plat_get_sensor_desc(uint8_t fru, sensor_desc_t **desc, size_t *desc_count)
{
  if (desc == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: desc is NULL\n", __func__);
    return -1;
  }
  if (desc_count == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: desc_count is NULL\n", __func__);
    return -1;
  }

  *desc = cri_sensor;
  *desc_count = ARRAY_SIZE(cri_sensor);

  return 0;
}

uint8_t plat_get_fru_sel(void)
{
  return FRU_SERVER;
}

int plat_get_mrc_desc(uint8_t fru, uint16_t major, uint16_t minor, char *desc)
{
  (void)fru;
  return pal_get_mrc_desc(major, minor, desc);
}

int plat_get_me_status(uint8_t fru, char *status)
{
  int ret = 0;
  ipmi_req_t_common_header req = {0};
  uint8_t rbuf[MAX_IPMB_RES_LEN] = {0};
  uint8_t rlen = 0;
  me_get_dev_id_res *res = (me_get_dev_id_res *)rbuf;

  if (status == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: status is NULL\n", __func__);
    return -1;
  }

  req.netfn_lun = IPMI_NETFN_SHIFT(NETFN_APP_REQ);
  req.cmd = CMD_APP_GET_DEVICE_ID;

  ret = bic_me_xmit((uint8_t *)(&req), sizeof(ipmi_req_t_common_header), (uint8_t *)res, &rlen);

  if (ret < 0 ) {
    syslog(LOG_WARNING, " %s ME transmission failed\n", __func__);
    return ret;
  }

  if (res->cc != CC_SUCCESS) {
    syslog(LOG_WARNING, " %s Fail to get device id: Completion Code: %02X\n", __func__, res->cc);
    return -1;
  }

  if (rlen != sizeof(me_get_dev_id_res)) {
    syslog(LOG_WARNING, " %s Invalid response length of get device id: %d, expected: %d\n", __func__, rlen, sizeof(me_get_dev_id_res));
    return -1;
  }

  if (BIT(res->ipmi_dev_id.fw_rev1, 7) == 1) {
    strcpy(status, "recovert_mode");
  } else {
    strcpy(status, "operation mode");
  }

  return 0;
}

int plat_get_board_id(char *id)
{
  uint8_t board_id = 0;

  if (id == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: id is NULL\n", __func__);
    return -1;
  }

  if(pal_get_uic_board_id(&board_id) < 0) {
    syslog(LOG_WARNING, " %s Fail to get uic board id\n", __func__);
    return -1;
  }

  snprintf(id, MAX_NUM_OF_BOARD_REV_ID_GPIO + SIZE_OF_NULL_TERMINATOR, "%u%u%u",
    BIT(board_id, 2), 
    BIT(board_id, 1),
    BIT(board_id, 0));

  return 0;
}

int plat_get_syscfg_text(uint8_t fru, char *syscfg)
{
  return -1;
}

int
plat_get_etra_fw_version(uint8_t fru_id, char *fw_text)
{
  return -1;
}

int plat_get_extra_sysinfo(uint8_t fru, char *info)
{
  char fru_name[MAX_FRU_CMD_STR] = {0};

  if (info == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: info is NULL\n", __func__);
    return -1;
  }

  if (pal_get_fru_name(fru, fru_name) < 0) {
    syslog(LOG_WARNING, " %s Fail to get fru name of FRU:%u\n", __func__, fru);
    return -1;
  }
  snprintf(info, sizeof("FRU:") + MAX_FRU_CMD_STR, "FRU:%s", fru_name);

  return 0;
}

int plat_udbg_get_uart_sel_num(uint8_t *uart_sel_num)
{
  if (uart_sel_num == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: uart_sel_num is NULL\n", __func__);
    return -1;
  }
  if (pal_get_debug_card_uart_sel(uart_sel_num) < 0) {
    syslog(LOG_WARNING, " %s Fail to get debug cart uart selection number\n", __func__);
    return -1;
  }

  return 0;
}

int plat_udbg_get_uart_sel_name(uint8_t uart_sel_num, char *uart_sel_name)
{
  if (uart_sel_name == NULL) {
    syslog(LOG_WARNING, " %s Invalid parameter: uart_sel_name is NULL\n", __func__);
    return -1;
  }

  switch (uart_sel_num) {
    case DEBUG_UART_SEL_BMC:
      snprintf(uart_sel_name, MAX_UART_SEL_NAME, "BMC");
      break;
    case DEBUG_UART_SEL_HOST:
      snprintf(uart_sel_name, MAX_UART_SEL_NAME, "SERVER");
      break;
    case DEBUG_UART_SEL_BIC:
      snprintf(uart_sel_name, MAX_UART_SEL_NAME, "BIC");
      break;
    case DEBUG_UART_SEL_EXP_SMART:
      snprintf(uart_sel_name, MAX_UART_SEL_NAME, "SCC_EXP_SMART");
      break;
    case DEBUG_UART_SEL_EXP_SDB:
      snprintf(uart_sel_name, MAX_UART_SEL_NAME, "SCC_EXP_SDB");
      break;
    case DEBUG_UART_SEL_IOC_T5_SMART:
      snprintf(uart_sel_name, MAX_UART_SEL_NAME, "SCC_IOC_SMART");
      break;
    case DEBUG_UART_SEL_IOC_T7_SMART:
      snprintf(uart_sel_name, MAX_UART_SEL_NAME, "IOCM_IOC_SMART");
      break;
    default:
      syslog(LOG_WARNING, " %s Invalid uart selection number: %u\n", __func__, uart_sel_num);
      return -1;
  }

  return 0;
}
