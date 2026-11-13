/*
 * Copyright (c) Nexthop Systems Inc
 *
 * Diagnostics helper for the Marvell OOB switch (88E6341 / 88E6191X) attached
 * to an ASPEED MDIO master. The RMON counters are only reachable
 * through a snapshot/read state machine in Global 1, which needs hundreds of
 * MDIO transactions per port; doing that from shell is far too slow, hence
 * this helper.
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
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include <openbmc/misc-utils.h>

#define BIT(n) (1U << (n))

/*
 * ASPEED AST2600 (G6) MDIO controller. The AST2700 (G7) has the same
 * registers at a different base, with three buses.
 */
#define AST_G6_MDIO_BASE 0x1e650000
#define AST_G6_MDIO_SIZE 0x1000
#define AST_G6_MDIO_MAX_BUS 4
#define AST_G7_MDIO_BASE 0x14040000
#define AST_G7_MDIO_MAX_BUS 3
#define AST_G6_MDIO_CFG 0x90000000 /* FIRE | MDC cycle threshold */
#define AST_G6_PHYCR_FIRE BIT(31)
#define AST_G6_PHYCR_READ BIT(27)
#define AST_G6_PHYCR_WRITE BIT(26)

static const uint32_t ast_g6_phycr_offset[] = {0x0, 0x8, 0x10, 0x18};
static const uint32_t ast_g6_phydata_offset[] = {0x4, 0xc, 0x14, 0x1c};

/* Marvell multi-chip SMI indirection registers (at the switch PHY address). */
#define MVL_SMI_CMD 0x0
#define MVL_SMI_DATA 0x1
#define MVL_SMI_CMD_BUSY 0x8000
#define MVL_SMI_CMD_CLAUSE22 0x1000
#define MVL_SMI_CMD_OP_WRITE (0x1 << 10)
#define MVL_SMI_CMD_OP_READ (0x2 << 10)

/* Marvell Global 1 statistics registers (SMI device 0x1b). */
#define MVL_GLOBAL1 0x1b
#define MVL_G1_STATS_OP 0x1d
#define MVL_G1_STATS_COUNTER_32 0x1e
#define MVL_G1_STATS_COUNTER_01 0x1f
#define MVL_G1_STATS_OP_BUSY 0x8000
#define MVL_G1_STATS_OP_READ_CAPTURED 0x4000
#define MVL_G1_STATS_OP_CAPTURE_PORT 0x5000
#define MVL_G1_STATS_OP_BANK1 0x0400

/* Switch Identifier, port register offset 0x03, product number in bits 15:4. */
#define MVL_PORT_SWITCH_ID 0x3

#define SMI_RETRY 200
#define SMI_PROBE_RETRY 3

enum stat_bank {
    BANK0,
    BANK1,
};

struct hw_stat {
    const char *name;
    uint8_t size; /* 4 or 8 bytes */
    uint8_t reg;
    enum stat_bank bank;
    int curated; /* part of the short, default counter set */
};

/*
 * Counter list and layout taken from the mv88e6xxx DSA driver
 * (mv88e6xxx_hw_stats). Both the 88E6341 and the 88E6191X expose bank 0 and
 * bank 1.
 */
static const struct hw_stat hw_stats[] = {
    {"in_good_octets", 8, 0x00, BANK0, 1},
    {"in_bad_octets", 4, 0x02, BANK0, 1},
    {"in_unicast", 4, 0x04, BANK0, 1},
    {"in_broadcasts", 4, 0x06, BANK0, 1},
    {"in_multicasts", 4, 0x07, BANK0, 1},
    {"in_pause", 4, 0x16, BANK0, 0},
    {"in_undersize", 4, 0x18, BANK0, 1},
    {"in_fragments", 4, 0x19, BANK0, 1},
    {"in_oversize", 4, 0x1a, BANK0, 1},
    {"in_jabber", 4, 0x1b, BANK0, 1},
    {"in_rx_error", 4, 0x1c, BANK0, 1},
    {"in_fcs_error", 4, 0x1d, BANK0, 1},
    {"out_octets", 8, 0x0e, BANK0, 1},
    {"out_unicast", 4, 0x10, BANK0, 1},
    {"out_broadcasts", 4, 0x13, BANK0, 1},
    {"out_multicasts", 4, 0x12, BANK0, 1},
    {"out_pause", 4, 0x15, BANK0, 0},
    {"excessive", 4, 0x11, BANK0, 0},
    {"collisions", 4, 0x1e, BANK0, 1},
    {"deferred", 4, 0x05, BANK0, 0},
    {"single", 4, 0x14, BANK0, 0},
    {"multiple", 4, 0x17, BANK0, 0},
    {"out_fcs_error", 4, 0x03, BANK0, 1},
    {"late", 4, 0x1f, BANK0, 1},
    {"hist_64bytes", 4, 0x08, BANK0, 0},
    {"hist_65_127bytes", 4, 0x09, BANK0, 0},
    {"hist_128_255bytes", 4, 0x0a, BANK0, 0},
    {"hist_256_511bytes", 4, 0x0b, BANK0, 0},
    {"hist_512_1023bytes", 4, 0x0c, BANK0, 0},
    {"hist_1024_max_bytes", 4, 0x0d, BANK0, 0},
    {"in_discards", 4, 0x00, BANK1, 1},
    {"in_filtered", 4, 0x01, BANK1, 1},
    {"in_accepted", 4, 0x02, BANK1, 0},
    {"in_bad_accepted", 4, 0x03, BANK1, 0},
    {"in_good_avb_class_a", 4, 0x04, BANK1, 0},
    {"in_good_avb_class_b", 4, 0x05, BANK1, 0},
    {"in_bad_avb_class_a", 4, 0x06, BANK1, 0},
    {"in_bad_avb_class_b", 4, 0x07, BANK1, 0},
    {"tcam_counter_0", 4, 0x08, BANK1, 0},
    {"tcam_counter_1", 4, 0x09, BANK1, 0},
    {"tcam_counter_2", 4, 0x0a, BANK1, 0},
    {"tcam_counter_3", 4, 0x0b, BANK1, 0},
    {"in_da_unknown", 4, 0x0e, BANK1, 0},
    {"in_management", 4, 0x0f, BANK1, 0},
    {"out_queue_0", 4, 0x10, BANK1, 0},
    {"out_queue_1", 4, 0x11, BANK1, 0},
    {"out_queue_2", 4, 0x12, BANK1, 0},
    {"out_queue_3", 4, 0x13, BANK1, 0},
    {"out_queue_4", 4, 0x14, BANK1, 0},
    {"out_queue_5", 4, 0x15, BANK1, 0},
    {"out_queue_6", 4, 0x16, BANK1, 0},
    {"out_queue_7", 4, 0x17, BANK1, 0},
    {"out_cut_through", 4, 0x18, BANK1, 0},
    {"out_octets_a", 4, 0x1a, BANK1, 0},
    {"out_octets_b", 4, 0x1b, BANK1, 0},
    {"out_management", 4, 0x1f, BANK1, 0},
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct switch_model {
    const char *name;
    uint16_t prod_num; /* Switch Identifier bits 15:4 */
    uint8_t phy_addr; /* MDIO address the switch answers on */
    uint8_t port_base; /* SMI device address of port 0 */
    uint8_t max_port;
    const char *default_ports;
};

/*
 * Board straps differ per switch: the 88E6191X answers at PHY 0x1e with ports
 * based at SMI device 0x0, the 88E6341 answers at PHY 0xf with ports based at
 * 0x10. This mirrors the probe order and default port lists in oob-status.sh.
 */
static const struct switch_model models[] = {
    {"88E6191X", 0x1920, 0x1e, 0x00, 10, "0 2 4 7 10"},
    {"88E6341", 0x3410, 0x0f, 0x10, 5, "0 1 2 4"},
};

struct mdio_soc {
    soc_model_t soc_model;
    off_t base; /* MDIO controller registers */
    uint32_t max_bus;
    uint32_t default_bus; /* bus the OOB switch hangs off */
};

/*
 * MDIO controller per SoC; the first entry is used when the SoC is not
 * listed.
 */
static const struct mdio_soc mdio_socs[] = {
    {SOC_MODEL_ASPEED_G6, AST_G6_MDIO_BASE, AST_G6_MDIO_MAX_BUS, 4},
    {SOC_MODEL_ASPEED_G7, AST_G7_MDIO_BASE, AST_G7_MDIO_MAX_BUS, 1},
};

static volatile uint32_t *mdio_regs;
static uint32_t mdio_bus; /* 1-based, matching mdio-util -m */
static uint8_t phy_addr;

/*
 * Set while walking the model table. An unpopulated MDIO address reads back as
 * 0xffff, which looks like a permanently busy SMI, so probe accesses give up
 * quickly and stay quiet instead of reporting a failure per candidate.
 */
static int probing;

static void short_delay(void)
{
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000};

    nanosleep(&ts, NULL);
}

/* Find the build's SoC model in mdio_socs[]. */
static const struct mdio_soc *detect_soc(void)
{
    soc_model_t soc_model = get_soc_model();
    size_t i;

    for (i = 0; i < ARRAY_SIZE(mdio_socs); i++) {
        if (mdio_socs[i].soc_model == soc_model)
            return &mdio_socs[i];
    }
    return &mdio_socs[0];
}

static int mdio_map(off_t mdio_base)
{
    long page = sysconf(_SC_PAGESIZE);
    off_t aligned = mdio_base & ~(off_t)(page - 1);
    void *base;
    int fd;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open /dev/mem: %s\n", strerror(errno));
        return -1;
    }

    base = mmap(NULL, AST_G6_MDIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                aligned);
    close(fd);
    if (base == MAP_FAILED) {
        fprintf(stderr, "Error: cannot mmap MDIO registers: %s\n",
                strerror(errno));
        return -1;
    }

    mdio_regs = (volatile uint32_t *)((char *)base + (mdio_base - aligned));
    return 0;
}

static uint32_t mdio_ctrl_read(void)
{
    return mdio_regs[ast_g6_phycr_offset[mdio_bus - 1] / 4];
}

static void mdio_ctrl_write(uint32_t val)
{
    mdio_regs[ast_g6_phycr_offset[mdio_bus - 1] / 4] = val;
}

static uint32_t mdio_data_read(void)
{
    return mdio_regs[ast_g6_phydata_offset[mdio_bus - 1] / 4];
}

static int mdio_wait_idle(void)
{
    int i;

    for (i = 0; i < 3000; i++) {
        if (!(mdio_ctrl_read() & AST_G6_PHYCR_FIRE))
            return 0;
        short_delay();
    }

    fprintf(stderr, "Error: MDIO bus %u stuck busy\n", mdio_bus);
    return -1;
}

static int mdio_read(uint8_t phy, uint8_t reg, uint16_t *val)
{
    uint32_t ctrl = AST_G6_MDIO_CFG | ((uint32_t)(phy & 0x1f) << 21) |
        ((uint32_t)(reg & 0x1f) << 16) | AST_G6_PHYCR_READ;

    if (mdio_wait_idle())
        return -1;

    mdio_ctrl_write(ctrl);
    if (mdio_wait_idle())
        return -1;

    *val = (uint16_t)(mdio_data_read() & 0xffff);
    return 0;
}

static int mdio_write(uint8_t phy, uint8_t reg, uint16_t val)
{
    uint32_t ctrl = AST_G6_MDIO_CFG | ((uint32_t)(phy & 0x1f) << 21) |
        ((uint32_t)(reg & 0x1f) << 16) | AST_G6_PHYCR_WRITE | val;

    if (mdio_wait_idle())
        return -1;

    mdio_ctrl_write(ctrl);
    return mdio_wait_idle();
}

/* Wait for the multi-chip SMI command register to accept the next access. */
static int smi_wait_ready(void)
{
    int retry = probing ? SMI_PROBE_RETRY : SMI_RETRY;
    uint16_t val;
    int i;

    for (i = 0; i < retry; i++) {
        if (mdio_read(phy_addr, MVL_SMI_CMD, &val))
            return -1;
        if (!(val & MVL_SMI_CMD_BUSY))
            return 0;
        short_delay();
    }

    if (!probing)
        fprintf(stderr, "Error: timed out waiting for SMI ready\n");

    return -1;
}

static int smi_read(uint8_t dev, uint8_t reg, uint16_t *val)
{
    uint16_t cmd = MVL_SMI_CMD_BUSY | MVL_SMI_CMD_CLAUSE22 |
        MVL_SMI_CMD_OP_READ | ((dev & 0x1f) << 5) | (reg & 0x1f);

    if (smi_wait_ready())
        return -1;
    if (mdio_write(phy_addr, MVL_SMI_CMD, cmd))
        return -1;
    if (smi_wait_ready())
        return -1;

    return mdio_read(phy_addr, MVL_SMI_DATA, val);
}

static int smi_write(uint8_t dev, uint8_t reg, uint16_t val)
{
    uint16_t cmd = MVL_SMI_CMD_BUSY | MVL_SMI_CMD_CLAUSE22 |
        MVL_SMI_CMD_OP_WRITE | ((dev & 0x1f) << 5) | (reg & 0x1f);

    if (smi_wait_ready())
        return -1;
    if (mdio_write(phy_addr, MVL_SMI_DATA, val))
        return -1;
    if (mdio_write(phy_addr, MVL_SMI_CMD, cmd))
        return -1;

    return smi_wait_ready();
}

static int g1_read(uint8_t reg, uint16_t *val)
{
    return smi_read(MVL_GLOBAL1, reg, val);
}

static int g1_write(uint8_t reg, uint16_t val)
{
    return smi_write(MVL_GLOBAL1, reg, val);
}

static int stats_wait(void)
{
    uint16_t val;
    int i;

    for (i = 0; i < SMI_RETRY; i++) {
        if (g1_read(MVL_G1_STATS_OP, &val))
            return -1;
        if (!(val & MVL_G1_STATS_OP_BUSY))
            return 0;
        short_delay();
    }

    fprintf(stderr, "Error: timed out waiting for statistics operation\n");
    return -1;
}

/*
 * Capture the counters of one port into the read-out registers. Both the 6341
 * and the 6191X use the 6390-style encoding where the port number is biased by
 * one and shifted into bits 9:5.
 */
static int stats_snapshot(int port)
{
    uint16_t op = MVL_G1_STATS_OP_BUSY | MVL_G1_STATS_OP_CAPTURE_PORT |
        (uint16_t)((port + 1) << 5);

    if (g1_write(MVL_G1_STATS_OP, op))
        return -1;

    return stats_wait();
}

/* Read one 32-bit captured counter. */
static int stats_read(uint16_t stat, uint32_t *val)
{
    uint16_t hi, lo;

    if (g1_write(MVL_G1_STATS_OP,
                 MVL_G1_STATS_OP_BUSY | MVL_G1_STATS_OP_READ_CAPTURED | stat))
        return -1;
    if (stats_wait())
        return -1;
    if (g1_read(MVL_G1_STATS_COUNTER_32, &hi))
        return -1;
    if (g1_read(MVL_G1_STATS_COUNTER_01, &lo))
        return -1;

    *val = ((uint32_t)hi << 16) | lo;
    return 0;
}

static int stat_value(const struct hw_stat *stat, uint64_t *out)
{
    uint16_t sel = stat->bank == BANK1 ? MVL_G1_STATS_OP_BANK1 : 0;
    uint32_t low, high = 0;

    if (stats_read(sel | stat->reg, &low))
        return -1;
    if (stat->size == 8 && stats_read(sel | (uint16_t)(stat->reg + 1), &high))
        return -1;

    *out = ((uint64_t)high << 32) | low;
    return 0;
}

/*
 * Probe each known (PHY addr, port base) pair for a matching Switch Identifier
 * and latch the globals the SMI helpers use.
 */
static const struct switch_model *detect_switch(void)
{
    const struct switch_model *found = NULL;
    uint16_t val;
    size_t i;

    probing = 1;
    for (i = 0; i < ARRAY_SIZE(models) && !found; i++) {
        phy_addr = models[i].phy_addr;

        if (smi_read(models[i].port_base, MVL_PORT_SWITCH_ID, &val))
            continue;
        if ((val & 0xfff0) == models[i].prod_num)
            found = &models[i];
    }
    probing = 0;

    if (!found) {
        fprintf(stderr, "Error: no supported OOB switch found on MDIO bus %u\n",
                mdio_bus);
        return NULL;
    }

    phy_addr = found->phy_addr;
    return found;
}

static int dump_port_counters(int port, int all)
{
    uint64_t val;
    size_t i;

    if (stats_snapshot(port))
        return -1;

    printf("\tPort %d:\n", port);
    for (i = 0; i < ARRAY_SIZE(hw_stats); i++) {
        if (!all && !hw_stats[i].curated)
            continue;
        if (stat_value(&hw_stats[i], &val))
            return -1;
        printf("\t\t%-24s %" PRIu64 "\n", hw_stats[i].name, val);
    }

    return 0;
}

static void usage(const char *prog)
{
    printf("Dump Marvell OOB switch (88E6341 / 88E6191X) diagnostics\n");
    printf("Usage: %s counters [OPTIONS] [Port(s)]\n", prog);
    printf("OPTIONS:\n");
    printf("  -a        Dump every RMON counter, not just the common ones\n");
    printf("  -m <bus>  MDIO master bus (default 4 on AST2600, 1 on AST2700, or $OOB_MDIO_BUS)\n");
    printf("  -h        Usage\n");
    printf("Example:\n");
    printf("  %s counters 0 4\n", prog);
}

static int parse_uint(const char *str, unsigned long *out)
{
    char *end;

    errno = 0;
    *out = strtoul(str, &end, 0);
    if (errno || *end != '\0' || end == str)
        return -1;

    return 0;
}

int main(int argc, char **argv)
{
    const struct switch_model *model;
    const struct mdio_soc *soc;
    unsigned long value;
    const char *env;
    char *ports, *tok;
    int all = 0;
    int rc = 0;
    int opt;

    if (argc < 2 || strcmp(argv[1], "counters") != 0) {
        usage(argv[0]);
        return argc >= 2 && strcmp(argv[1], "-h") == 0 ? 0 : 1;
    }

    soc = detect_soc();
    mdio_bus = soc->default_bus;
    env = getenv("OOB_MDIO_BUS");
    if (env && !parse_uint(env, &value))
        mdio_bus = (uint32_t)value;

    optind = 2;
    while ((opt = getopt(argc, argv, "am:h")) != -1) {
        switch (opt) {
            case 'a':
                all = 1;
                break;
            case 'm':
                if (parse_uint(optarg, &value)) {
                    fprintf(stderr, "Error: invalid bus '%s'\n", optarg);
                    return 1;
                }
                mdio_bus = (uint32_t)value;
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (mdio_bus < 1 || mdio_bus > soc->max_bus) {
        fprintf(stderr, "Error: MDIO bus must be 1-%u\n", soc->max_bus);
        return 1;
    }

    if (mdio_map(soc->base))
        return 1;

    model = detect_switch();
    if (!model)
        return 1;

    printf("Switch: %s\n", model->name);
    printf("Counters:\n");

    if (optind < argc) {
        int i;

        for (i = optind; i < argc; i++) {
            if (parse_uint(argv[i], &value) || value > model->max_port) {
                fprintf(stderr, "Invalid port %s for %s (valid: 0-%u)\n",
                        argv[i], model->name, model->max_port);
                return 1;
            }
        }
        for (i = optind; i < argc && !rc; i++) {
            parse_uint(argv[i], &value);
            rc = dump_port_counters((int)value, all);
        }
        return rc ? 1 : 0;
    }

    ports = strdup(model->default_ports);
    if (!ports) {
        fprintf(stderr, "Error: out of memory\n");
        return 1;
    }

    for (tok = strtok(ports, " "); tok && !rc; tok = strtok(NULL, " ")) {
        if (parse_uint(tok, &value)) {
            rc = -1;
            break;
        }
        rc = dump_port_counters((int)value, all);
    }
    free(ports);

    return rc ? 1 : 0;
}
