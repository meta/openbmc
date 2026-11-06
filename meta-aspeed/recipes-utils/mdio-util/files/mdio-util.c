/*
 * Copyright 2020-present Facebook. All Rights Reserved.
 *
 * This is the source file of Watchdog Control Command Line Interface.
 * Run "mdio_bus-util -h" on openbmc for usage information.
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <syslog.h>
#include <getopt.h>

#define BIT(x)     (1 << x)
#define HEX_BASE   16

#define MAX_PHY_ADDR 0x1f

extern char *optarg;
static int debug=0;
//distinguish chip
#define SCU0_BASE_ADDR                          0x12C02000
#define SCU1_BASE_ADDR                          0x14C02000
#define SCU_BASE_ADDR                           0x1E6E2000
#define SILICON_REVISION_ID_OFFSET_AST_G5       0x7C // AST_G5
#define SILICON_REVISION_ID_0_OFFSET_AST_G6     0x04 // AST_G6
#define SILICON_REVISION_ID_1_OFFSET_AST_G6     0x14 // AST_G6
#define SILICON_REVISION_ID_OFFSET_AST_G7       0x00 // AST_G7
#define AST_G5_REV_ID                           0x04000000
#define AST_G6_REV_ID                           0x05000000
#define AST_G7_REV_ID                           0x06000000

//AST_G7
#define AST_G7_MDIO_MAX_BUS            3
#define AST_G7_IO_BASE             0x14040000
#define AST_G7_PHYCR_READ_BIT      BIT(27)
#define AST_G7_PHYCR_WRITE_BIT     BIT(26)
#define AST_G7_PHYCR_BUSY_BIT      BIT(31)
static char ast_g7_phycr_offset[] = {0x0, 0x8, 0x10};
static char ast_g7_phydata_offset[]={0x4, 0xc, 0x14};

//AST_G6
#define AST_G6_MDIO_MAX_BUS            4
#define AST_G6_IO_BASE             0x1e650000
#define AST_G6_PHYCR_READ_BIT      BIT(27)
#define AST_G6_PHYCR_WRITE_BIT     BIT(26)
#define AST_G6_PHYCR_BUSY_BIT      BIT(31)
static char ast_g6_phycr_offset[] = {0x0, 0x8, 0x10, 0x18};
static char ast_g6_phydata_offset[]={0x4, 0xc, 0x14, 0x1c};

//AST_G5
#define AST_G5_MDIO_MAX_BUS            2
static uint32_t ast_g5_io_base[]={0x1E660000, 0x1E680000};
uint32_t PHYCR_READ_BIT_AST_G5 = 0x1 << 26;
uint32_t PHYCR_WRITE_BIT_AST_G5 = 0x1 << 27;
#define AST_G5_PHYCR_REG_OFFSET  0x60
#define AST_G5_PHYDATA_REG_OFFSET  0x64

#define DEVMEM_PATH "/sbin/devmem"

enum {
    READ_OP,
    WRITE_OP,
};

enum chip {
    INVALID_CHIP,
    AST_G5,
    AST_G6,
    AST_G7,
};

static int run_devmem(uint32_t reg_addr, uint32_t *value, int operation)
{
    char str[64] = {0};
    char buff[64] = {0};
    int ret = -1;
    FILE *fp;
    if(READ_OP == operation) {
        snprintf(str, sizeof(str), "%s 0x%x", DEVMEM_PATH, reg_addr);
        if(debug)
            printf("Debug: read command %s\n",str);
        fp = popen(str, "r");
        if(!fgets(buff, sizeof(buff), fp))
            goto exit;
        *value = strtoul(buff, NULL, HEX_BASE);
        if(debug)
            printf("Debug: read result is 0x%x\n",*value);
    } else if(WRITE_OP == operation) {
        snprintf(str, sizeof(str), "%s 0x%x 32 0x%x", DEVMEM_PATH,
                 reg_addr, *value);
        if(debug)
            printf("Debug: write command is %s\n", str);
        fp = popen(str, "r");
    }
    ret = 0;

exit:
    pclose(fp);
    return ret;
}

// Check and wait R/W operation to complete
static int wait_complete_operation(uint32_t chip, uint32_t mdio_bus) {
  uint32_t ctrl_status = 0;
  uint32_t mdio_addr = 0;
  int count = 0;
  int max_count = 300;
  int query_delay = 10000;  //unit in us, 10000us = 10ms
  uint32_t status_bit_mask = 0;

  if (chip == AST_G5) {
    mdio_addr = ast_g5_io_base[mdio_bus - 1] + AST_G5_PHYCR_REG_OFFSET;
    status_bit_mask = PHYCR_READ_BIT_AST_G5 | PHYCR_WRITE_BIT_AST_G5;
  } else if (chip == AST_G6) {
    mdio_addr = AST_G6_IO_BASE + ast_g6_phycr_offset[mdio_bus - 1];
    status_bit_mask = AST_G6_PHYCR_BUSY_BIT;
  } else if (chip == AST_G7) {
    mdio_addr = AST_G7_IO_BASE + ast_g7_phycr_offset[mdio_bus - 1];
    status_bit_mask = AST_G7_PHYCR_BUSY_BIT;
  } else {
    return -1;
  }

  if (run_devmem(mdio_addr, &ctrl_status, READ_OP) == -1)
    return -1;

  while (ctrl_status & status_bit_mask) {
    usleep(query_delay);
    if (run_devmem(mdio_addr, &ctrl_status, READ_OP) == -1 || count > max_count)
      return -1;
    count++;
  }

  return 0;
}

//AST_G5
static int read_mdio_ast_g5(uint32_t mdio_bus, uint32_t phy, uint32_t phy_reg)
{
    uint32_t mdio_addr = 0;
    uint32_t ctrl = 0;
    mdio_addr = ast_g5_io_base[mdio_bus - 1] + AST_G5_PHYCR_REG_OFFSET;
    if(run_devmem(mdio_addr, &ctrl, READ_OP) == -1)
        return -1;
    ctrl &= 0x3000003F;
    ctrl |= ((phy & 0x1F) << 16);
    ctrl |= ((phy_reg & 0x1F) << 21);
    ctrl |= PHYCR_READ_BIT_AST_G5;

    if(run_devmem(mdio_addr, &ctrl, WRITE_OP) == -1)
        return -1;

    if(wait_complete_operation(AST_G5, mdio_bus) == -1)
        return -1;

    mdio_addr = ast_g5_io_base[mdio_bus - 1] + AST_G5_PHYDATA_REG_OFFSET;
    if(run_devmem(mdio_addr, &ctrl, READ_OP) == -1)
        return -1;

    ctrl = ctrl >> 16;
    printf("Read from PHY 0x%x.0x%x, value is 0x%x\n", phy, phy_reg, ctrl);
    return 0;
}

static int write_mdio_ast_g5(uint32_t mdio_bus, uint32_t phy,
                                  uint32_t phy_reg, uint32_t value)
{
    uint32_t mdio_addr = 0;
    uint32_t ctrl = 0;
    uint32_t data_reg = 0;
    mdio_addr = ast_g5_io_base[mdio_bus - 1] + AST_G5_PHYCR_REG_OFFSET;
    if(run_devmem(mdio_addr, &ctrl, READ_OP) == -1)
        return -1;
    ctrl &= 0x3000003F;
    ctrl |= ((phy & 0x1F) << 16);
    ctrl |= ((phy_reg & 0x1F) << 21);
    ctrl |= PHYCR_WRITE_BIT_AST_G5;
    
    data_reg = ast_g5_io_base[mdio_bus - 1] + AST_G5_PHYDATA_REG_OFFSET;
    if(run_devmem(data_reg, &value, WRITE_OP) == -1)
        return -1;
    if(run_devmem(mdio_addr, &ctrl, WRITE_OP) == -1)
        return -1;

    return 0;
}

//ast_g6
static int read_mdio_ast_g6(uint32_t mdio_bus, uint32_t phy, uint32_t phy_reg)
{
    uint32_t mdio_addr = 0;
    uint32_t ctrl = 0;
    mdio_addr = AST_G6_IO_BASE + ast_g6_phycr_offset[mdio_bus - 1];
    if(run_devmem(mdio_addr, &ctrl, READ_OP) == -1)
        return -1;
    ctrl |= 0x90000000;
    ctrl &= 0x90000000;
    ctrl |= ((phy & 0x1F) << 21);
    ctrl |= ((phy_reg & 0x1F) << 16);
    ctrl |= AST_G6_PHYCR_READ_BIT;
    if(run_devmem(mdio_addr, &ctrl, WRITE_OP) == -1)
        return -1;

    if(wait_complete_operation(AST_G6, mdio_bus) == -1)
        return -1;

    mdio_addr = AST_G6_IO_BASE + ast_g6_phydata_offset[mdio_bus - 1];
    if(run_devmem(mdio_addr, &ctrl, READ_OP) == -1)
        return -1;
    ctrl &= 0x0000ffff;
    printf("Read from PHY 0x%x.0x%x, value is 0x%x\n", phy, phy_reg, ctrl);
    return 0;
}

static int write_mdio_ast_g6(uint32_t mdio_bus, uint32_t phy,
                                    uint32_t phy_reg, uint32_t value)
{
    uint32_t mdio_addr = 0;
    uint32_t ctrl = 0;
    uint32_t temp_value = value;
    mdio_addr = AST_G6_IO_BASE + ast_g6_phycr_offset[mdio_bus - 1];
    if(run_devmem(mdio_addr, &ctrl, READ_OP) == -1)
        return -1;
    ctrl |= 0x90000000;
    ctrl &= 0x90000000;
    ctrl |= ((phy & 0x1F) << 21);
    ctrl |= ((phy_reg & 0x1F) << 16);
    ctrl |= AST_G6_PHYCR_WRITE_BIT;
    temp_value |= ctrl;
    if(run_devmem(mdio_addr, &temp_value, WRITE_OP) == -1)
        return -1;

    return 0;
}

//ast_g7
static int read_mdio_ast_g7(uint32_t mdio_bus, uint32_t phy, uint32_t phy_reg)
{
    uint32_t mdio_addr = 0;
    uint32_t ctrl = 0;
    mdio_addr = AST_G7_IO_BASE + ast_g7_phycr_offset[mdio_bus - 1];
    if(run_devmem(mdio_addr, &ctrl, READ_OP) == -1)
        return -1;
    ctrl |= 0x90000000;
    ctrl &= 0x90000000;
    ctrl |= ((phy & 0x1F) << 21);
    ctrl |= ((phy_reg & 0x1F) << 16);
    ctrl |= AST_G7_PHYCR_READ_BIT;
    if(run_devmem(mdio_addr, &ctrl, WRITE_OP) == -1)
        return -1;

    if(wait_complete_operation(AST_G7, mdio_bus) == -1)
        return -1;

    mdio_addr = AST_G7_IO_BASE + ast_g7_phydata_offset[mdio_bus - 1];
    if(run_devmem(mdio_addr, &ctrl, READ_OP) == -1)
        return -1;
    ctrl &= 0x0000ffff;
    printf("Read from PHY 0x%x.0x%x, value is 0x%x\n", phy, phy_reg, ctrl);
    return 0;
}

static int write_mdio_ast_g7(uint32_t mdio_bus, uint32_t phy,
                                    uint32_t phy_reg, uint32_t value)
{
    uint32_t mdio_addr = 0;
    uint32_t ctrl = 0;
    uint32_t temp_value = value;
    mdio_addr = AST_G7_IO_BASE + ast_g7_phycr_offset[mdio_bus - 1];
    if(run_devmem(mdio_addr, &ctrl, READ_OP) == -1)
        return -1;
    ctrl |= 0x90000000;
    ctrl &= 0x90000000;
    ctrl |= ((phy & 0x1F) << 21);
    ctrl |= ((phy_reg & 0x1F) << 16);
    ctrl |= AST_G7_PHYCR_WRITE_BIT;
    temp_value |= ctrl;
    if(run_devmem(mdio_addr, &temp_value, WRITE_OP) == -1)
        return -1;

    return 0;
}

static int read_mdio_op(uint32_t chip, uint32_t mdio_bus, uint32_t phy,
                        uint32_t phy_reg) {
  int ret = 0 ;
  switch (chip) {
    case AST_G5:
      ret = read_mdio_ast_g5(mdio_bus, phy, phy_reg);
      break;
    case AST_G6:
      ret = read_mdio_ast_g6(mdio_bus, phy, phy_reg);
      break;
    case AST_G7:
      ret = read_mdio_ast_g7(mdio_bus, phy, phy_reg);
      break;
    default:
      return -1;
  }

  return ret;
}

static int write_mdio_op(uint32_t chip, uint32_t mdio_bus, uint32_t phy,
                        uint32_t phy_reg, uint32_t value) {
  int ret = 0 ;
  switch (chip) {
    case AST_G5:
      ret = write_mdio_ast_g5(mdio_bus, phy, phy_reg, value);
      break;
    case AST_G6:
      ret = write_mdio_ast_g6(mdio_bus, phy, phy_reg, value);
      break;
    case AST_G7:
      ret = write_mdio_ast_g7(mdio_bus, phy, phy_reg, value);
      break;
    default:
      return -1;
  }

  if (ret) {
    return -1;
  }

  if (wait_complete_operation(chip, mdio_bus) == -1) {
    return -1;
  }

  printf("Write to PHY 0x%x.0x%x, value is 0x%x\n", phy, phy_reg, value);
  return 0;
}

void print_usage()
{
    printf("Usage: [OPTIONS...]\n");
    printf("OPTIONS:\n");
    printf("  -m        Specific mdio bus\n");
    printf("  -p        Specific phy address\n");
    printf("  -r [hex]  Phy register to read\n");
    printf("  -w [hex]  Phy register to write\n");
    printf("  -d [hex]  Data to write\n");
    printf("  -h|?      Usage\n");
    printf("  -D        Use debug mode\n");
    printf("Example: \n");
    printf("  mdio-util -m 2 -p 0x0d -r 0x0\n");
    printf("  mdio-util -m 2 -p 0x0d -w 0x0 -d 0x0\n");
    return;
}

/*
 * ASPEED G5 Silicon Revision ID: SCU[7C]
 * ASPEED G6 Silicon Revision ID: SCU[04]/SCU[14]
 * ASPEED G7 Silicon Revision ID: SCU0[00]/SCU1[00]
 * AST G5 SCU[04] 
 */
static int check_chip()
{
    uint32_t cpu_silicon_rev_id_0;
    uint32_t cpu_silicon_rev_id_1;
    uint32_t chip = INVALID_CHIP;
    if(debug)
        printf("Debug: detecting chip ...\n");
    run_devmem(SCU0_BASE_ADDR + SILICON_REVISION_ID_OFFSET_AST_G7,
               &cpu_silicon_rev_id_0, READ_OP);
    run_devmem(SCU1_BASE_ADDR + SILICON_REVISION_ID_OFFSET_AST_G7,
               &cpu_silicon_rev_id_1, READ_OP);
    if(cpu_silicon_rev_id_0 == cpu_silicon_rev_id_1 &&
      (cpu_silicon_rev_id_0 & AST_G7_REV_ID) == AST_G7_REV_ID) {
        chip = AST_G7; //AST_G7
        goto exit;
    }

    run_devmem(SCU_BASE_ADDR + SILICON_REVISION_ID_0_OFFSET_AST_G6,
               &cpu_silicon_rev_id_0, READ_OP);
    run_devmem(SCU_BASE_ADDR + SILICON_REVISION_ID_1_OFFSET_AST_G6,
               &cpu_silicon_rev_id_1, READ_OP);
    if(cpu_silicon_rev_id_0 == cpu_silicon_rev_id_1 &&
      (cpu_silicon_rev_id_0 & AST_G6_REV_ID) == AST_G6_REV_ID) {
        chip = AST_G6; //AST_G6
        goto exit;
    }

    run_devmem(SCU_BASE_ADDR + SILICON_REVISION_ID_OFFSET_AST_G5,
               &cpu_silicon_rev_id_0, READ_OP);
    if((cpu_silicon_rev_id_0 & AST_G5_REV_ID) == AST_G5_REV_ID) {
        chip = AST_G5; //AST_G5
    } else {
        chip = INVALID_CHIP;
    }

exit:
    if(debug) {
        switch(chip) {
        case AST_G7:
            printf("Debug: detected chip AST_G7\n");
        break;
        case AST_G6:
            printf("Debug: detected chip AST_G6\n");
        break;
        case AST_G5:
            printf("Debug: detected chip AST_G5\n");
        break;
        case INVALID_CHIP:
            printf("Debug: detected chip is not supported\n");
        break;
        }
    }

    return chip;
}

int main(int argc, char **argv)
{
    char opt;
    uint32_t chip;
    int read_flag = 0;
    int write_flag = 0;
    uint32_t mdio_bus = 0;
    uint32_t phy = MAX_PHY_ADDR + 1;
    uint32_t phy_reg = 0;
    uint32_t max_bus;
    int data = -1;

    if(argc <= 1) {
        print_usage();
        return 0;
    }

    while((opt = getopt(argc, argv, "m:p:r:w:d:hD")) != (char)-1) {
        switch (opt) {
        case 'm':
            mdio_bus = strtoul(optarg, NULL, HEX_BASE);
            break;
        case 'p':
            phy = strtoul(optarg, NULL, HEX_BASE);
            if(phy > MAX_PHY_ADDR) {
                printf("PHY address must be smaller than 0x%x.\n", MAX_PHY_ADDR);
                return -1;
            }
            break;
        case 'r':
            read_flag = 1;
            phy_reg = strtoul(optarg, NULL, HEX_BASE);
            break;
        case 'w':
            write_flag = 1;
            phy_reg = strtoul(optarg, NULL, HEX_BASE);
            break;
        case 'd':
            data = strtoul(optarg, NULL ,HEX_BASE);
            break;
        case 'h':
            print_usage();
            return 0;
        case 'D':
            debug = 1;
            break;
        default:
            print_usage();
            break;
        }
    }

    chip = check_chip();
    if(chip == INVALID_CHIP) {
        printf("Chip is not supportted\n");
        return -1;
    }

    switch (chip) {
    case AST_G7:
        max_bus = AST_G7_MDIO_MAX_BUS;
        break;
    case AST_G6:
        max_bus = AST_G6_MDIO_MAX_BUS;
        break;
    default:
        max_bus = AST_G5_MDIO_MAX_BUS;
        break;
    }

    if (mdio_bus > max_bus) {
        printf("-m can only be less than %d\n", max_bus);
        return -1;
    }

    if(write_flag == read_flag) {
        printf("Read/write at the same time is not support, please correct argument\n");
        return -1;
    }
    if(write_flag == 1 && data == -1) {
        printf("Please correct argument -d\n");
        return -1;
    }
    if(read_flag == 1 && data != -1) {
        printf("Please discard argument -d\n");
        return -1;
    }

    if (read_flag) {
      if (debug) {
        printf("Debug: read mdio: %d, phy: 0x%x, reg: 0x%x ...\n",
                mdio_bus, phy, phy_reg);
      }
      if (read_mdio_op(chip, mdio_bus, phy, phy_reg)) {
        printf("Error: failed to read mdio: %d from PHY 0x%x.0x%x\n",
                mdio_bus, phy, phy_reg);
        return -1;
      }
    }

    if (write_flag) {
      if (debug) {
        printf("Debug: write mdio: %d, phy: 0x%x, reg: 0x%x, value: 0x%x ...\n",
                mdio_bus, phy, phy_reg, data);
      }
      if (write_mdio_op(chip, mdio_bus, phy, phy_reg, data)) {
        printf("Error: failed to write mdio: %d to PHY 0x%x.0x%x, value is 0x%x\n",
                mdio_bus, phy, phy_reg, data);
        return -1;
      }
    }

    return 0;
}
