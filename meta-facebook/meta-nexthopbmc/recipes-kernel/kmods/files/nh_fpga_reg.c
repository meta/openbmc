// SPDX-License-Identifier: GPL-2.0
/*
 * Character file driver for accessing FPGA through SPI.
 *
 * Copyright (c) 2026 Nexthop Systems Inc.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/unaligned.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>

#define FPGA_REG_DRIVER_NAME "fpga_reg_driver"
#define FPGA_REG_CLASS_NAME "fpga_reg"

#define FPGA_CMD_WRITE 0x01
#define FPGA_CMD_READ  0x02

#define FPGA_REG_SIZE 0x100000

/*
 * FPGA SPI protocol, expressed as spi-mem ops (CMD/ADDR/DUMMY/DATA):
 * Reads:  [CMD 0x02][ADDR 4B][DUMMY = TA CFG bytes][DATA IN 4B]
 * Writes: [CMD 0x01][ADDR 4B]                      [DATA OUT 4B]
 *
 * spi-mem is used (rather than plain spi_sync transfers) because the
 * AST26xx spi-aspeed-smc controller driver only implements spi-mem
 * exec_op and rejects generic transfers with -ENOTSUPP.
 */
#define FPGA_ADDR_LEN   4
#define FPGA_DATA_LEN   4

static u8 fpga_ta_len = 4;

static int set_fpga_ta_len(const char *val, const struct kernel_param *kp)
{
    int ret;
    int data;

    ret = kstrtoint(val, 10, &data);
    if (ret)
        return ret;

    if (data < 0 || data > 255) {
        pr_err("fpga_reg_driver: Value %d is out of range (0-255)!\n", data);
        return -EINVAL;
    }

    fpga_ta_len = (u8) data;
    return 0;
}

static const struct kernel_param_ops fpga_ta_ops = {
    .set = set_fpga_ta_len,
    .get = param_get_byte, // Standard getter
};

module_param_cb(fpga_ta_len, &fpga_ta_ops, &fpga_ta_len, 0644);
MODULE_PARM_DESC(fpga_ta_len, "FPGA TA byte len for the driver");

struct fpga_data {
    struct spi_mem *mem;
    struct spi_device *spi;

    dev_t devt;
    struct device *device;

    struct cdev cdev;
    struct class *class;
    u64 current_addr;

    /* spi-mem requires a DMA-able (non-stack) data buffer */
    struct mutex buf_lock;
    u8 *data_buf;
};

static int fpga_spi_read_reg(struct fpga_data *data, u32 addr, u32 *val)
{
    struct spi_mem_op op =
        SPI_MEM_OP(SPI_MEM_OP_CMD(FPGA_CMD_READ, 1),
                   SPI_MEM_OP_ADDR(FPGA_ADDR_LEN, addr, 1),
                   SPI_MEM_OP_DUMMY(fpga_ta_len, 1),
                   SPI_MEM_OP_DATA_IN(FPGA_DATA_LEN, data->data_buf, 1));
    int ret;

    mutex_lock(&data->buf_lock);
    ret = spi_mem_exec_op(data->mem, &op);
    if (!ret)
        *val = get_unaligned_be32(data->data_buf);
    mutex_unlock(&data->buf_lock);

    return ret;
}

static int fpga_spi_write_reg(struct fpga_data *data, u32 addr, u32 val)
{
    struct spi_mem_op op =
        SPI_MEM_OP(SPI_MEM_OP_CMD(FPGA_CMD_WRITE, 1),
                   SPI_MEM_OP_ADDR(FPGA_ADDR_LEN, addr, 1),
                   SPI_MEM_OP_NO_DUMMY,
                   SPI_MEM_OP_DATA_OUT(FPGA_DATA_LEN, data->data_buf, 1));
    int ret;

    mutex_lock(&data->buf_lock);
    put_unaligned_be32(val, data->data_buf);
    ret = spi_mem_exec_op(data->mem, &op);
    mutex_unlock(&data->buf_lock);

    return ret;
}

/* --- File Operations --- */
static loff_t fpga_spi_llseek(struct file *filp, loff_t offset, int whence) {
    struct fpga_data *data = filp->private_data;
    loff_t new_pos;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = data->current_addr + offset;
            break;
        case SEEK_END:
            new_pos = FPGA_REG_SIZE + offset;
            break;
        default:
            return -EINVAL;
    }

    if (new_pos < 0 || new_pos > FPGA_REG_SIZE)
        return -EINVAL;
    if (new_pos & 0x3)
        return -EINVAL;

    data->current_addr = new_pos;

    return new_pos;
}

static ssize_t fpga_spi_read(struct file *filp, char __user *buffer, size_t len, loff_t *ptr) {
    struct fpga_data *data = filp->private_data;
    size_t count = 0;
    u32 val;
    int ret;

    if (data->current_addr & 0x3 || len & 0x3)
        return -EINVAL;

    while (count < len) {
        dev_dbg(&data->spi->dev, "read: addr=0x%llx", data->current_addr);

        ret = fpga_spi_read_reg(data, data->current_addr, &val);
        if (ret)
            return count ? (ssize_t)count : ret;

        dev_dbg(&data->spi->dev, "read result: 0x%08x", val);

        if (copy_to_user(buffer + count, &val, sizeof(val)))
            return -EFAULT;

        data->current_addr += 4;
        count += 4;
    }

    return (ssize_t)count;
}

static ssize_t fpga_spi_write(struct file *filp, const char __user *buffer, size_t len, loff_t *ptr) {
    struct fpga_data *data = filp->private_data;
    size_t count = 0;
    u32 val;
    int ret;

    if (data->current_addr & 0x3 || len & 0x3)
        return -EINVAL;

    while (count < len) {
        if (copy_from_user(&val, buffer + count, sizeof(val)))
            return -EFAULT;

        dev_dbg(&data->spi->dev, "write: addr=0x%llx val=0x%08x", data->current_addr, val);

        ret = fpga_spi_write_reg(data, data->current_addr, val);
        if (ret)
            return count ? (ssize_t)count : ret;

        data->current_addr += 4;
        count += 4;
    }

    return (ssize_t)count;
}

static int fpga_spi_open(struct inode *inode, struct file *filp) {
    filp->private_data = container_of(inode->i_cdev, struct fpga_data, cdev);
    return 0;
}

static const struct file_operations fpga_fops = {
    .owner = THIS_MODULE,
    .open = fpga_spi_open,
    .read = fpga_spi_read,
    .write = fpga_spi_write,
    .llseek = fpga_spi_llseek,
};

static int fpga_spi_probe(struct spi_mem *mem) {
    struct spi_device *spi = mem->spi;
    struct fpga_data *data;
    u8 cs = spi_get_chipselect(spi, 0);
    u32 val = (u32)-1;
    int ret;

    dev_info(&spi->dev, "Probing SPI device (bus_num=%d, chip_select=%d)",
        spi->controller->bus_num, cs);

    data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->mem = mem;
    data->spi = spi;
    spi_mem_set_drvdata(mem, data);

    data->data_buf = devm_kmalloc(&spi->dev, FPGA_DATA_LEN, GFP_KERNEL);
    if (!data->data_buf)
        return -ENOMEM;
    mutex_init(&data->buf_lock);

    ret = fpga_spi_read_reg(data, 0x00000000, &val);
    if (ret) {
        dev_err(&spi->dev, "Failed to read FPGA ID register: %i", ret);
        return ret;
    }

    dev_info(&spi->dev, "Detected FPGA ID %08x", val);

    /* Allocate device numbers */
    ret = alloc_chrdev_region(&data->devt, 0, 1, FPGA_REG_CLASS_NAME);
    if (ret) {
        dev_err(&spi->dev, "Unable to allocate major number: %i", ret);
        return ret;
    }

    dev_info(&spi->dev, "%s device allocated, dev [%i,%i]", FPGA_REG_CLASS_NAME,
            MAJOR(data->devt), MINOR(data->devt));

    data->class = class_create(FPGA_REG_DRIVER_NAME);
    if (IS_ERR(data->class)) {
        ret = PTR_ERR(data->class);
        dev_err(&spi->dev, "Unable to create %s class %d", FPGA_REG_CLASS_NAME, ret);
        goto err_class;
    }

    cdev_init(&data->cdev, &fpga_fops);
    cdev_add(&data->cdev, data->devt, 1);

    data->device = device_create(data->class, NULL, data->devt, NULL, FPGA_REG_CLASS_NAME);
    if (IS_ERR(data->device)) {
        ret = PTR_ERR(data->device);
        dev_err(&spi->dev, "Unable to create %s device %d", FPGA_REG_CLASS_NAME, ret);
        goto err;
    }

    dev_info(&spi->dev, "Probe successful /dev/%s", FPGA_REG_CLASS_NAME);
    return 0;

err:
    cdev_del(&data->cdev);
    class_destroy(data->class);
err_class:
    unregister_chrdev_region(data->devt, 1);
    return ret;
}

static int fpga_spi_remove(struct spi_mem *mem) {
    struct fpga_data *data = spi_mem_get_drvdata(mem);

    dev_info(&mem->spi->dev, "Removing /dev/%s", FPGA_REG_CLASS_NAME);

    device_destroy(data->class, data->devt);
    cdev_del(&data->cdev);
    class_destroy(data->class);
    unregister_chrdev_region(data->devt, 1);

    dev_info(&mem->spi->dev, "Remove successful /dev/%s", FPGA_REG_CLASS_NAME);
    return 0;
}

static const struct of_device_id fpga_spi_of_match[] = {
    { .compatible = "nh,fpga-spi-bridge" },
    { },
};
MODULE_DEVICE_TABLE(of, fpga_spi_of_match);

static const struct spi_device_id nh_fpga_spi_ids[] = {
    { "fpga-spi-bridge", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, nh_fpga_spi_ids);

static struct spi_mem_driver fpga_spi_driver = {
    .spidrv = {
        .driver = {
            .name = FPGA_REG_DRIVER_NAME,
            .of_match_table = fpga_spi_of_match,
        },
        .id_table = nh_fpga_spi_ids,
    },
    .probe = fpga_spi_probe,
    .remove = fpga_spi_remove,
};

module_spi_mem_driver(fpga_spi_driver);

MODULE_AUTHOR("Nexthop AI");
MODULE_DESCRIPTION("FPGA Register Access Driver");
MODULE_LICENSE("GPL v2");
