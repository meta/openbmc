#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <openbmc/misc-utils.h>
#include <openbmc/obmc-pal.h>
#include <openbmc/kv.h>
#include "mp29608a.h"

#define DEBUG 0

#define VR_PAGE_0 0x00
#define VR_PAGE_1 0x01
#define VR_PAGE_SINGLE_CONFIG_MAX 0x0E

/*Page0 */
#define VR_CMD_WRITE_PROTECT 0x10
#define VR_CMD_STORE_USER_CODE 0x17
#define VR_CMD_RESTORE_USER_CODE 0x18
#define VP_CMD_PMBUS_REVISION 0x98
#define VR_CMD_MFR_ID 0x99
#define VR_CMD_MFR_CONFIG_ID 0x9E
#define VR_CMD_IC_DEVICE_ID 0xAD
#define VR_CMD_READ_CRC_REG 0xED

/*Page1 */
#define VR_CMD_MFR_MTP_MEMORY_CTRL 0xCC

#define MP2869_DEVICE_ID 0x00002869
#define MP29612_DEVICE_ID 0x00029612
#define MP29608_DEVICE_ID 0x00029608
#define MP2869A_DEVICE_ID 0x0000869A
#define MP29612A_DEVICE_ID 0x0000612A
#define MP29608A_DEVICE_ID 0x0000608A
#define MP2869B_DEVICE_ID 0x0000869B
#define MP29612B_DEVICE_ID 0x0000612B
#define MP29608B_DEVICE_ID 0x0000608B
#define UNKNOWN_DEVICE_ID 0xFFFFFFFF

#define VR_VALUE_DISABLE_WRITE_PROTECT 0x00
#define VR_VALUE_PMBUS_REVISION 0x33
#define VR_VALUE_MFR_ID 0x4D5053

#define VR_PROGRAM_RETRY 50

#define MAX_PRODUCT_ID_LEN 4
#define MAX_PMBUS_REVISION_LEN 1
#define MAX_MFR_ID_LEN 3

enum { ATE_CONF_ID = 0,
       ATE_PAGE_NUM,
       ATE_REG_ADDR_HEX,
       ATE_REG_ADDR_DEC,
       ATE_REG_NAME,
       ATE_REG_DATA_HEX,
       ATE_REG_DATA_DEC,
       ATE_WRITE_TYPE,
       ATE_COL_MAX,
};

extern int vr_rdwr(uint8_t, uint8_t, uint8_t *, uint8_t, uint8_t *, uint8_t);
static int (*vr_xfer)(uint8_t bus, uint8_t addr, uint8_t *tbuf, uint8_t tcnt, uint8_t *rbuf,
		      uint8_t rcnt) = &vr_rdwr;

static int mp29608a_set_page(uint8_t bus, uint8_t addr, uint8_t page)
{
	uint8_t tbuf[2] = { VR_REG_PAGE, page };
	if (vr_xfer(bus, addr, tbuf, 2, NULL, 0)) {
		syslog(LOG_WARNING, "%s: set page to 0x%02X failed", __func__, tbuf[1]);
		return VR_STATUS_FAILURE;
	}
	return VR_STATUS_SUCCESS;
}

static int mp29608a_get_product_id(uint8_t bus, uint8_t addr, uint8_t *product_id)
{
	uint8_t tbuf[1] = { VR_CMD_IC_DEVICE_ID };
	uint8_t rbuf[MAX_PRODUCT_ID_LEN + 1] = { 0 };
	if (mp29608a_set_page(bus, addr, VR_PAGE_0)) {
		return VR_STATUS_FAILURE;
	}
	if (vr_xfer(bus, addr, tbuf, 1, rbuf, MAX_PRODUCT_ID_LEN + 1)) {
		syslog(LOG_WARNING, "%s: read 0x%02X failed", __func__, tbuf[0]);
		return VR_STATUS_FAILURE;
	}
	memcpy(product_id, rbuf + 1, MAX_PRODUCT_ID_LEN);
	return VR_STATUS_SUCCESS;
}

static int mp29608a_check_product_id(uint8_t bus, uint8_t addr, uint32_t product_id_exp)
{
	uint8_t product_id[4];
	if (mp29608a_get_product_id(bus, addr, product_id)) {
		return VR_STATUS_FAILURE;
	}
	if (product_id[0] == (product_id_exp & 0xFF) &&
	    product_id[1] == ((product_id_exp >> 8) & 0xFF) &&
	    product_id[2] == ((product_id_exp >> 16) & 0xFF) &&
	    product_id[3] == ((product_id_exp >> 24) & 0xFF)) {
		return VR_STATUS_SUCCESS;
	}
	else {
		printf("%s: check product id failed, reg: %02x%02x%02x%02x, expect: %08x\n", __func__, 
			product_id[3], product_id[2], product_id[1], product_id[0], product_id_exp);
		return VR_STATUS_FAILURE;
	}
}

static int mp29608a_unlock_write_protect_mode(uint8_t bus, uint8_t addr)
{
	uint8_t tbuf[2] = { VR_CMD_WRITE_PROTECT,
			    VR_VALUE_DISABLE_WRITE_PROTECT };
	if (mp29608a_set_page(bus, addr, VR_PAGE_0)) {
		return VR_STATUS_FAILURE;
	}
	if (vr_xfer(bus, addr, tbuf, 2, NULL, 0)) {
		syslog(LOG_WARNING, "%s: write 0x%02X failed", __func__, tbuf[0]);
		return VR_STATUS_FAILURE;
	}
	return VR_STATUS_SUCCESS;
}

static int mp29608a_check_pmbus_revision(uint8_t bus, uint8_t addr)
{
	uint8_t tbuf[1] = { VP_CMD_PMBUS_REVISION };
	uint8_t rbuf[MAX_PMBUS_REVISION_LEN] = { 0 };
	if (mp29608a_set_page(bus, addr, VR_PAGE_0)) {
		return VR_STATUS_FAILURE;
	}
	for (int retry = VR_PROGRAM_RETRY; retry > 0; retry--) {
		if (vr_xfer(bus, addr, tbuf, 1, rbuf, MAX_PMBUS_REVISION_LEN)) {
			syslog(LOG_WARNING, "%s: read 0x%02X failed", __func__, tbuf[0]);
			return VR_STATUS_FAILURE;
		}
		if (rbuf[0] == VR_VALUE_PMBUS_REVISION) {
			return VR_STATUS_SUCCESS;
		}
	}
	printf("%s: check pmbus revision failed, reg: %02x, expect: %02x\n", __func__, rbuf[0], VR_VALUE_PMBUS_REVISION);
	return VR_STATUS_FAILURE;
}

static int mp29608a_check_mfr_id(uint8_t bus, uint8_t addr)
{
	uint8_t tbuf[1] = { VR_CMD_MFR_ID };
	uint8_t rbuf[MAX_MFR_ID_LEN + 1] = { 0 };
	if (mp29608a_set_page(bus, addr, VR_PAGE_0)) {
		return VR_STATUS_FAILURE;
	}
	if (vr_xfer(bus, addr, tbuf, 1, rbuf, MAX_MFR_ID_LEN + 1)) {
		syslog(LOG_WARNING, "%s: read 0x%02X failed", __func__, tbuf[0]);
		return VR_STATUS_FAILURE;
	}
	if (rbuf[1] == (VR_VALUE_MFR_ID & 0xFF) &&
	    rbuf[2] == ((VR_VALUE_MFR_ID >> 8) & 0xFF) &&
	    rbuf[3] == ((VR_VALUE_MFR_ID >> 16) & 0xFF)) {
		return VR_STATUS_SUCCESS;
	} else {
		printf("%s: check mfr id failed, reg: %02x%02x%02x, expect: %06x\n", __func__, rbuf[3], rbuf[2], rbuf[1], VR_VALUE_MFR_ID);
		return VR_STATUS_FAILURE;
	}
}

static int mp29608a_get_crc(uint8_t bus, uint8_t addr, uint16_t *crc)
{
	uint8_t tbuf[1] = { VR_CMD_READ_CRC_REG };
	uint8_t rbuf[MAX_CRC_LEN] = { 0 };
	if (crc == NULL) {
		syslog(LOG_WARNING, "%s: invalid parameter pointer is NULL", __func__);
		return VR_STATUS_FAILURE;
	}

	if (mp29608a_set_page(bus, addr, VR_PAGE_0)) {
		return VR_STATUS_FAILURE;
	}
	if (vr_xfer(bus, addr, tbuf, 1, rbuf, MAX_CRC_LEN)) {
		syslog(LOG_WARNING, "%s: read 0x%02X failed", __func__, tbuf[0]);
		return VR_STATUS_FAILURE;
	}
	memcpy(crc, rbuf, MAX_CRC_LEN);
	return VR_STATUS_SUCCESS;
}

static int mp29608a_check_crc(uint8_t bus, uint8_t addr, uint8_t *crc)
{
	uint16_t crc_get;
	if (mp29608a_get_crc(bus, addr, &crc_get)) {
		syslog(LOG_WARNING, "%s: get CRC failed", __func__);
		return VR_STATUS_FAILURE;
	}
	if (((crc_get & 0xFF) == crc[0]) && (((crc_get >> 8) & 0xFF) == crc[1])) {
		return VR_STATUS_SUCCESS;
	}
	else {
		printf("%s: check crc failed, reg: %04x, expect: %02x%02x\n", __func__, crc_get, crc[1], crc[0]);
		return VR_STATUS_FAILURE;
	}
}

int program_mp29608a(uint8_t bus, uint8_t addr, struct mp29608a_config *config, bool force)
{
	uint8_t tbuf[16], rbuf[16];
	uint8_t txlen = 0;
	uint8_t page_current = 0;
	int i = 0;
	struct mp29608a_data *data;

	uint16_t crc_get;
	if (mp29608a_get_crc(bus, addr, &crc_get)) {
		syslog(LOG_WARNING, "%s: get CRC failed", __func__);
		return VR_STATUS_FAILURE;
	}

	if (!force && 
		(((crc_get & 0xFF) == config->crc_code[0]) && (((crc_get >> 8) & 0xFF) == config->crc_code[1]))) {
		printf("the checksum is the same as the current firmware %02x%02x!\n", 
			config->crc_code[1], config->crc_code[0]);
		printf("Please use \"--force\" option to try again.\n");
		syslog(LOG_WARNING, "%s: redundant programming", __func__);
		return VR_STATUS_FAILURE;
	}

	if (mp29608a_unlock_write_protect_mode(bus, addr)) {
		syslog(LOG_WARNING, "%s: unlock write protect failed", __func__);
		return VR_STATUS_FAILURE;
	}

	if (mp29608a_set_page(bus, addr, VR_PAGE_0)) {
		return VR_STATUS_FAILURE;
	}
	printf("write data start\n");

	for (i = 0; i < config->wr_cnt; i++) {
		data = &config->pdata[i];
		if (data->page != page_current) {
			if (mp29608a_set_page(bus, addr, data->page)) {
				return VR_STATUS_FAILURE;
			}
			page_current = data->page;
		}

		tbuf[0] = data->reg_addr;
		memcpy(&tbuf[1], data->reg_data, data->reg_data_len);
		txlen = data->reg_data_len + 1;
#if DEBUG == 1
		printf("page: %2d, tx:", data->page);
		for (int i_tx = 0; i_tx < txlen; i_tx++)
			printf(" %02x", tbuf[i_tx]);
		printf(", txlen=%d\n", txlen);
#endif
		if (vr_xfer(bus, addr, tbuf, txlen, NULL, 0)) {
			syslog(LOG_WARNING, "%s: write 0x%02X failed", __func__, tbuf[0]);
			return VR_STATUS_FAILURE;
		}
#if DEBUG == 0
		printf("\rupdated: %d %%  ", ((i + 1) * 100) / config->wr_cnt);
		fflush(stdout);
#endif
	}

	printf("write data finish, start set and check\n");
	// Turn to page 1. Write Page1@CCh, Bit[0]=1,Bit[5]=1,Bit[7]=0,Bit[12]=0, keep other Bits
	if (mp29608a_set_page(bus, addr, VR_PAGE_1)) {
		return VR_STATUS_FAILURE;
	}
	tbuf[0] = VR_CMD_MFR_MTP_MEMORY_CTRL;
	if (vr_xfer(bus, addr, tbuf, 1, rbuf, 2)) {
		syslog(LOG_WARNING, "%s: read 0x%02X failed", __func__, tbuf[0]);
		return VR_STATUS_FAILURE;
	}
	tbuf[1] = (rbuf[0] & 0x7F) | 0x21;
	tbuf[2] = rbuf[1] & 0xEF;
	if (vr_xfer(bus, addr, tbuf, 3, NULL, 0)) {
		syslog(LOG_WARNING, "%s: write 0x%02X failed", __func__, tbuf[0]);
		return VR_STATUS_FAILURE;
	}

	// Turn to page 0. Send no byte command 17h to store data into MTP
	if (mp29608a_set_page(bus, addr, VR_PAGE_0)) {
		return VR_STATUS_FAILURE;
	}
	tbuf[0] = VR_CMD_STORE_USER_CODE;
	if (vr_xfer(bus, addr, tbuf, 1, NULL, 0)) {
		syslog(LOG_WARNING, "%s: write 0x%02X failed", __func__, tbuf[0]);
		return VR_STATUS_FAILURE;
	}

	// Wait 50ms + 2ms*change_reg
	msleep(50 + 2 * config->wr_cnt);

	// Repeat Read 98h 1byte util result is 8'h33
	if (mp29608a_check_pmbus_revision(bus, addr)) {
		syslog(LOG_WARNING, "%s: check PMBUS Revision failed", __func__);
		return VR_STATUS_FAILURE;
	}

	// Result_0ED == GUI_CRC?
	if (mp29608a_check_crc(bus, addr, config->crc_code)) {
		syslog(LOG_WARNING, "%s: check CRC failed, before restore", __func__);
		return VR_STATUS_FAILURE;
	}

	// Send no byte command 18h to restore data, wait 10ms
	if (mp29608a_set_page(bus, addr, VR_PAGE_0)) {
		return VR_STATUS_FAILURE;
	}
	tbuf[0] = VR_CMD_RESTORE_USER_CODE;
	if (vr_xfer(bus, addr, tbuf, 1, NULL, 0)) {
		syslog(LOG_WARNING, "%s: write 0x%02X failed", __func__, tbuf[0]);
		return VR_STATUS_FAILURE;
	}

	msleep(10);

	// Result_0ED == GUI_CRC?
	if (mp29608a_check_crc(bus, addr, config->crc_code)) {
		syslog(LOG_WARNING, "%s: check CRC failed, after restore", __func__);
		return VR_STATUS_FAILURE;
	}

	return VR_STATUS_SUCCESS;
}

static int cache_mp29608a_crc(uint8_t bus, uint8_t addr, char *key, char *ver_str)
{
	if ((key == NULL) || (ver_str == NULL)) {
		syslog(LOG_WARNING, "%s: invalid parameter pointer is NULL", __func__);
		return VR_STATUS_FAILURE;
	}

	uint16_t crc;
	if (mp29608a_get_crc(bus, addr, &crc)) {
		syslog(LOG_WARNING, "%s: get CRC failed", __func__);
		return VR_STATUS_FAILURE;
	}

	snprintf(ver_str, MAX_VALUE_LEN, "MPS %04X", crc);
	kv_set(key, ver_str, 0, 0);

	return VR_STATUS_SUCCESS;
}

int mp29608a_fw_update(struct vr_info *info, void *args)
{
	struct mp29608a_config *config = (struct mp29608a_config *)args;

	if (info == NULL || config == NULL) {
		return VR_STATUS_FAILURE;
	}

	printf("Update VR: %s\n", info->dev_name);
	if ((info->addr >> 1) != config->addr) {
		printf("ERROR: The 7-bit address in the FW file is 0x%02x, but the device address is 0x%02x\n",
			config->addr, info->addr >> 1);
		syslog(LOG_WARNING, "%s: address mismatch; please use the correct FW file", __func__);
		return VR_STATUS_FAILURE;
	}

	if (info->xfer) {
		vr_xfer = info->xfer;
	}

	// page0@99h with 3 bytes read == 0x4d5053?,	VR_CMD_MFR_ID
	if (mp29608a_check_mfr_id(info->bus, info->addr)) {
		syslog(LOG_WARNING, "%s: check mfr id failed", __func__);
		return VR_STATUS_FAILURE;
	}
	// page0@adh with 4 bytes read,					VR_CMD_IC_DEVICE_ID
	if (mp29608a_check_product_id(info->bus, info->addr, config->product_id_exp)) {
		syslog(LOG_WARNING, "%s: check product id failed", __func__);
		return VR_STATUS_FAILURE;
	}
	// page0@9eh with 2 bytes read,					VR_CMD_MFR_CONFIG_ID

	if (program_mp29608a(info->bus, info->addr, config, info->force)) {
		syslog(LOG_WARNING, "%s: program failed", __func__);
		return VR_STATUS_FAILURE;
	}
	return VR_STATUS_SUCCESS;
}

void *mp29608a_parse_file(struct vr_info *info, const char *path)
{
	if (mp29608a_check_mfr_id(info->bus, info->addr)) {
		printf("%s: the VR on bus %d, addr 0x%02x is not an MPS device\n",
			__func__, info->bus, info->addr);
		syslog(LOG_WARNING, "%s: check mfr id failed", __func__);
		return NULL;
	}
	char line[120], *str;
	char buf[10] = { 0 };
	FILE *fp = NULL;
	int dcnt = 0;
	int col = 0;
	uint8_t len;
	struct mp29608a_config *config = NULL;

	fp = fopen(path, "r");
	if (!fp) {
		printf("ERROR: invalid file path!\n");
		return NULL;
	}

	config = (struct mp29608a_config *)calloc(1, sizeof(struct mp29608a_config));
	if (config == NULL) {
		printf("ERROR: no space for creating config!\n");
		fclose(fp);
		return NULL;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		if (!strncmp(line, "END", 3)) {
			break;
		}

		//configure ID,		page number, 		register address(hex),	register address(dec),
		//register name,	register data(hex),	register data(dec),		Write Type
		//ATE_CONF_ID,		ATE_PAGE_NUM,		ATE_REG_ADDR_HEX,		ATE_REG_ADDR_DEC,
		//ATE_REG_NAME,		ATE_REG_DATA_HEX,	ATE_REG_DATA_DEC,		ATE_WRITE_TYPE
		for (col = 0; col < ATE_COL_MAX; col++) {
			if (col == ATE_CONF_ID)
				str = strtok(line, "\t"); //Col 1
			else
				str = strtok(NULL, "\t"); //Col 2~N
			switch (col) {
			case ATE_PAGE_NUM:
				config->pdata[dcnt].page = strtol(str, NULL, 10);
				break;
			case ATE_REG_ADDR_HEX:
				config->pdata[dcnt].reg_addr = strtol(str, NULL, 16);
				break;
			case ATE_REG_DATA_HEX:
				if (dcnt >= MAX_ATE_DATA_NUM) {
					free(config);
					config = NULL;
					printf("ERROR: ATE data count over %d!\n", MAX_ATE_DATA_NUM);
					return config;
				}
				len = strlen(str) / 2;
				if (len >= MAX_REG_DATA_LEN) {
					free(config);
					config = NULL;
					printf("ERROR: data: %s too long, over %d!\n", str, MAX_REG_DATA_LEN);
					return config;
				}
				for (int i = 0; i < len; i++) {
					memcpy(buf, &(str[i * 2]), 2);
					config->pdata[dcnt].reg_data[len - 1 - i] =
						(uint8_t)strtol(buf, NULL, 16);
				}
				config->pdata[dcnt].reg_data_len = len;
#if DEBUG == 1
				printf("Page: %2d, Reg: 0x%02X, ", config->pdata[dcnt].page,
				       config->pdata[dcnt].reg_addr);
				printf("Count: %03d, Data:", dcnt);
				for (int i = 0; i < config->pdata[dcnt].reg_data_len; i++)
					printf(" %02X", config->pdata[dcnt].reg_data[i]);
				printf("\n");
#endif
				dcnt++;
				break;
			default:
				break;
			}
			// only need to config command rows of 1X
			// and just ignore command rows of 2X, 3X, 4X, 5X and 6X
			// when using single configuration flow charts.
			if (config->pdata[dcnt].page > VR_PAGE_SINGLE_CONFIG_MAX) {
				break;
			}
			config->pdata[dcnt].page = config->pdata[dcnt].page % 10;
		}
	}
	config->wr_cnt = dcnt;

	while (fgets(line, sizeof(line), fp) != NULL) {
		if (!strncmp(line, "CRC_CHECK_START", 15)) {
			// CRC_CHECK_START
			// 0000	1	F0	240	CRC_USER	DA43	55875	2
			// CRC_CHECK_STOP
			while (fgets(line, sizeof(line), fp) != NULL) {
				if (!strncmp(line, "CRC_CHECK_STOP", 14)) {
					break;
				}
				for (col = 0; col < ATE_COL_MAX; col++) {
					if (col == ATE_CONF_ID)
						str = strtok(line, "\t"); //Col 1
					else
						str = strtok(NULL, "\t"); //Col 2~N
					switch (col) {
					case ATE_REG_DATA_HEX:
						len = strlen(str) / 2;
						for (int i = 0; i < len; i++) {
							memcpy(buf, &(str[i * 2]), 2);
							config->crc_code[len - 1 - i] =
								(uint8_t)strtol(buf, NULL, 16);
						}
#if DEBUG == 1
						printf("CRC: %02x %02x\n", config->crc_code[1],
						       config->crc_code[0]);
#endif
						break;
					default:
						break;
					}
				}
			}
		} else if (!strncmp(line, "Product ID:", 11)) {
			// Product ID:	MP29608-A
			str = strtok(line, "\t");
			str = strtok(NULL, "\t");
			if (!strncmp(str, "MP29608-A", 9)) {
				config->product_id_exp = MP29608A_DEVICE_ID;
			} else if (!strncmp(str, "MP29608-B", 9)) {
				config->product_id_exp = MP29608B_DEVICE_ID;
			} else {
				config->product_id_exp = UNKNOWN_DEVICE_ID;
			}
#if DEBUG == 1
			printf("Product: %08x\n", config->product_id_exp);
#endif
		} else if (!strncmp(line, "I2C Address:", 12)) {
			// I2C Address:	63	99
			str = strtok(line, "\t");
			str = strtok(NULL, "\t");
			config->bus = 0;
			config->addr = strtol(str, NULL, 16);
#if DEBUG == 1
			printf("Bus: %d, Addr: 0x%02x\n", config->bus, config->addr);
#endif
		}
	}

	fclose(fp);

	return config;
}

int get_mp29608a_ver(struct vr_info *info, char *ver_str)
{
	if ((info == NULL) || (ver_str == NULL)) {
		syslog(LOG_WARNING, "%s: invalid parameter pointer is NULL", __func__);
		return VR_STATUS_FAILURE;
	}

	char key[MAX_KEY_LEN] = { 0 };
	size_t max_ver_str_len = MAX_VER_STR_LEN;

	if (info->private_data) {
		snprintf(key, sizeof(key), "%s_vr_%02xh_checksum", (char *)info->private_data,
			 info->addr);
	} else {
		snprintf(key, sizeof(key), "vr_%02xh_checksum", info->addr);
	}

	if (kv_get(key, ver_str, &max_ver_str_len, 0)) {
		if (info->xfer) {
			vr_xfer = info->xfer;
		}

		//Stop sensor polling before read crc from register
		if (info->sensor_polling_ctrl != NULL) {
			info->sensor_polling_ctrl(false);
		}

		if (cache_mp29608a_crc(info->bus, info->addr, key, ver_str)) {
			//Resume sensor polling
			if (info->sensor_polling_ctrl != NULL) {
				info->sensor_polling_ctrl(true);
			}
			return VR_STATUS_FAILURE;
		}

		//Resume sensor polling
		if (info->sensor_polling_ctrl != NULL) {
			info->sensor_polling_ctrl(true);
		}
	}

	return VR_STATUS_SUCCESS;
}
