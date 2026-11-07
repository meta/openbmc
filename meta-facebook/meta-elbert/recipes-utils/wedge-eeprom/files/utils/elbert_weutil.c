/*
 * Copyright 2020-present Facebook. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>

#include <facebook/wedge_eeprom.h>
#include <facebook/elbert_eeprom.h>
#include <string.h>
#define BMC_MGMT_MACADDR "/sys/class/net/eth0/address"

void read_local_mac(char *buffer, size_t buffer_len)
{
  if (!buffer || buffer_len == 0)
    return;
  FILE *fp;
  snprintf(buffer, buffer_len, "NA");
  fp = fopen(BMC_MGMT_MACADDR, "r");
  if (fp){
     if (fscanf(fp, "%19s", buffer) != 1) {
       snprintf(buffer, buffer_len, "NA");
     }
     fclose(fp);
  }
  return;
}

/*
 * Every EEPROM elbert exposes, in the order show_tech collected them.
 */
static const char *elbert_targets[] = {
  "CHASSIS", "SMB", "SCM",
  "PIM2", "PIM3", "PIM4", "PIM5", "PIM6", "PIM7", "PIM8", "PIM9",
  "SMB_EXTRA", "BMC",
};
#define ELBERT_NUM_TARGETS \
  (sizeof(elbert_targets) / sizeof(elbert_targets[0]))

static void usage(const char *prog)
{
  printf("Usage: %s [-a | --all | <device>]\n", prog);
  printf("   -a | --all : print every EEPROM device\n");
  printf("   <device>   : one of");
  for (size_t i = 0; i < ELBERT_NUM_TARGETS; i++)
    printf(" %s", elbert_targets[i]);
  printf(" (default CHASSIS)\n");
}

/*
 * Following fruid info translates to thrift structures in Fboss
 * and has a dependency in FBNet
 */

static int dump_eeprom(const char *fn)
{
  struct wedge_eeprom_st eeprom;
  int rc;
  char local_mac[20];
  uint8_t parsed_mac[6];

  rc = elbert_eeprom_parse(fn, &eeprom);
  if (rc) {
    printf("%d\n", rc);
    fprintf(stderr, "Failed to parse %s EEPROM\n", fn ? fn : "default");
    return -1;
  }

  read_local_mac(local_mac, sizeof(local_mac));
  elbert_parse_mac(parsed_mac,local_mac,0);

  printf("Wedge EEPROM %s:\n", fn ? fn : "");
  printf("Version: %d\n", eeprom.fbw_version);
  printf("Product Name: %s\n", eeprom.fbw_product_name);
  // Use product_asset instead of product_number as it is larger
  printf("Product Part Number: %s\n", eeprom.fbw_product_asset);
  printf("System Assembly Part Number: %s\n", eeprom.fbw_assembly_number);
  printf("Facebook PCBA Part Number: %s\n", eeprom.fbw_facebook_pcba_number);
  printf("Facebook PCB Part Number: %s\n", eeprom.fbw_facebook_pcb_number);
  printf("ODM PCBA Part Number: %s\n", eeprom.fbw_odm_pcba_number);
  printf("ODM PCBA Serial Number: %s\n", eeprom.fbw_odm_pcba_serial);
  printf("Product Production State: %d\n", eeprom.fbw_production_state);
  printf("Product Version: %d\n", eeprom.fbw_product_version);
  printf("Product Sub-Version: %d\n", eeprom.fbw_product_subversion);
  printf("Product Serial Number: %s\n", eeprom.fbw_product_serial);
  // Elbert Asset Tag not supported
  printf("Product Asset Tag: \n" );
  printf("System Manufacturer: %s\n", eeprom.fbw_system_manufacturer);
  printf("System Manufacturing Date: %s\n",
         eeprom.fbw_system_manufacturing_date);
  printf("PCB Manufacturer: %s\n", eeprom.fbw_pcb_manufacturer);
  printf("Assembled At: %s\n", eeprom.fbw_assembled);
  printf("Local MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
         parsed_mac[0], parsed_mac[1], parsed_mac[2], parsed_mac[3],
         parsed_mac[4], parsed_mac[5]);
  printf("Extended MAC Base: %02X:%02X:%02X:%02X:%02X:%02X\n",
         eeprom.fbw_mac_base[0], eeprom.fbw_mac_base[1],
         eeprom.fbw_mac_base[2], eeprom.fbw_mac_base[3],
         eeprom.fbw_mac_base[4], eeprom.fbw_mac_base[5]);
  printf("Extended MAC Address Size: %d\n", eeprom.fbw_mac_size);
  printf("Location on Fabric: %s\n", eeprom.fbw_location);
  printf("CRC8: 0x%x\n", eeprom.fbw_crc8);

  return 0;
}

int main(int argc, const char *argv[])
{
  const char *fn;

  if (argc >= 2 &&
      (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    usage(argv[0]);
    return 0;
  }

  if (argc >= 2 &&
      (!strcmp(argv[1], "-a") || !strcmp(argv[1], "--all"))) {
    int failed = 0;

    /*
     * Keep going when a device is absent: on a partly populated chassis
     * some PIM slots are empty, and the rest of the dump is still useful.
     */
    for (size_t i = 0; i < ELBERT_NUM_TARGETS; i++) {
      if (i)
        printf("\n");
      if (dump_eeprom(elbert_targets[i]))
        failed++;
    }
    return failed == (int)ELBERT_NUM_TARGETS ? -1 : 0;
  }

  if (argc >= 2) {
    fn = argv[1];
  } else {
    fn = "CHASSIS";
  }

  return dump_eeprom(fn) ? -1 : 0;
}
