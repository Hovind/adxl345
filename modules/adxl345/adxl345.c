/* adxl345.c */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <asm/uaccess.h>

#define NAME        "adxl345"
#define AXES        3
#define BUFFER_SIZE 64

struct adxl345_device {
    unsigned int axis;
    signed char data[BUFFER_SIZE];

    wait_queue_head_t queue;

    struct miscdevice miscdev;
};

enum ADXL345_CMD { ADXL345_SET_AXIS_X = 1024, ADXL345_SET_AXIS_Y, ADXL345_SET_AXIS_Z };

static int adxl345_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int adxl345_remove(struct i2c_client *client);
static irqreturn_t adxl345_irq(int irq, void *dev_id);

int adxl345_open(struct inode *inode, struct file *file);
ssize_t adxl345_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos);
long adxl345_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#define REG_BW_RATE         0x2C
#define REG_POWER_CTL       0x2D
#define REG_INT_ENABLE      0x2E
#define REG_DATA_FORMAT     0x31
#define REG_FIFO_CTL        0x38

#define STREAM_MODE  (1 << 7)
#define WATERMARK    (1 << 1)
#define MEASURE_MODE (1 << 3)
#define STANDBY_MODE 0

#define DATA_RATE_100HZ     0x0A
#define FORMAT_DEFAULT      0
#define SAMPLES             20

static const char DEVID[] = { 0x00 };
static const char DATAX[] = { 0x32 };
static const char DATAY[] = { 0x34 };
static const char DATAZ[] = { 0x36 };
static const char DATA[] = { 0x32, 0x33, 0x34, 0x35, 0x36, 0x37 };

static const char INIT_RATE[] = {
    REG_BW_RATE,
    DATA_RATE_100HZ,
};

static const char INIT_FORMAT[] = {
    REG_DATA_FORMAT,
    FORMAT_DEFAULT,
};

static const char INIT_FIFO[] = {
    REG_FIFO_CTL,
    STREAM_MODE | SAMPLES,
};

static const char INIT_POWER_AND_INT[] = {
    REG_POWER_CTL,
    MEASURE_MODE,
    WATERMARK,
};

static const char DEINIT[] = {
    REG_POWER_CTL,
    STANDBY_MODE,
};

static const struct file_operations adxl345_fops = {
    /* .open = adxl345_open, */
    .read = adxl345_read,
    .unlocked_ioctl = adxl345_unlocked_ioctl,
};

static int adxl345_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct adxl345_device *adxl345dev;
    char dev_id;
    int ret;

    /* Allocate adxl345_device */
    adxl345dev = devm_kzalloc(&client->dev, sizeof(struct adxl345_device), GFP_KERNEL);
    if (!adxl345dev)
        return ENOMEM;

    /* Initialize adxl345_device */
    adxl345dev->miscdev.minor  = MISC_DYNAMIC_MINOR;
    adxl345dev->miscdev.name   = NAME;
    adxl345dev->miscdev.fops   = &adxl345_fops;
    adxl345dev->miscdev.parent = &client->dev;
    adxl345dev->miscdev.mode   = S_IRUGO | S_IWUGO;

    /* Initialize waitqueue */
    init_waitqueue_head(&adxl345dev->queue);

    /* Link from i2c_client to adxl345_device */
    i2c_set_clientdata(client, adxl345dev);

    /* Register device */
    ret = misc_register(&adxl345dev->miscdev); // Check return value

    /* Request irq */
    ret = devm_request_threaded_irq(&client->dev, client->irq, NULL, adxl345_irq,
                              IRQF_ONESHOT, client->name, client);

    /* Check irq request */
    if (ret < 0) {
        pr_info("adxl345: Failed to register IRQ\n");
        return ret; 
    }

    /* Read DEVID from adxl345 */
    i2c_master_send(client, DEVID, 1);
    i2c_master_recv(client, &dev_id, 1);

    /* Configure adxl345 for measuring mode */
    i2c_master_send(client, INIT_RATE, ARRAY_SIZE(INIT_RATE));
    i2c_master_send(client, INIT_FORMAT, ARRAY_SIZE(INIT_FORMAT));
    i2c_master_send(client, INIT_FIFO, ARRAY_SIZE(INIT_FIFO));
    i2c_master_send(client, INIT_POWER_AND_INT, ARRAY_SIZE(INIT_POWER_AND_INT));

    /* Print */
    pr_info("adxl345: Module loaded for device with DEVID %02x\n", dev_id);
 
    return ret;
}

static int adxl345_remove(struct i2c_client *client)
{
    struct adxl345_device *adxl345dev;

    /* Get adxl345_device */
    adxl345dev = i2c_get_clientdata(client);

    /* Deregister device */
    misc_deregister(&adxl345dev->miscdev); // Return value?

    /* Configure adxl345 for standby mode */
    i2c_master_send(client, DEINIT, ARRAY_SIZE(DEINIT));

    /* Print */
    pr_info("adxl345: Module unloaded\n");
    return 0;
}

static irqreturn_t adxl345_irq(int irq, void *dev_id)
{
    struct i2c_client *client;
    struct adxl345_device *adxl345dev;
    char tmp[2 * BUFFER_SIZE];
    int i;

    /* Get i2c_client */
    client = dev_id; 

    /* Get adxl345_device */
    adxl345dev = i2c_get_clientdata(client);

    /* Print */
    pr_info("adxl345: Handling IRQ\n");

    /* Read adxl345 DATA to buffer */
    for (i = 0; i < SAMPLES; i++) {
        i2c_master_send(client, DATA, 1);
        i2c_master_recv(client, tmp + 2 * AXES * i, 2 * AXES);
    }

    /* Extract pair values */
    for (i = 0; i < SAMPLES * AXES; i++) {
        adxl345dev->data[i] = tmp[2 * i];
    }

    /* u16 le16_to_cpu(__le16) */

    /* Wake up queue */
    wake_up(&adxl345dev->queue);
    return IRQ_HANDLED;
}

ssize_t adxl345_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos)
{
    struct miscdevice *mdev;
    struct adxl345_device *adxl345dev;
    int i;

    /* Get adxl345_device */
    mdev = file->private_data;
    adxl345dev = container_of(mdev, struct adxl345_device, miscdev);

    /* Print
    pr_info("adxl345: Reading axis %d\n", adxl345dev->axis); */

    /* Wait for queue */
    wait_event(adxl345dev->queue, 1);

    /* Get data size
    size = sizeof(adxl345dev->data[0]); */

    /* Get from user */
    for (i = 0; i < count; ++i) {
        if (copy_to_user(buf + i, adxl345dev->data + AXES * i + adxl345dev->axis, 1))
           return i;
    }
    
    return count;
}

long adxl345_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct miscdevice *mdev;
    struct adxl345_device *adxl345dev;

    /* Get adxl345_device */
    mdev = file->private_data;
    adxl345dev = container_of(mdev, struct adxl345_device, miscdev);

    /* Print */
    pr_info("adxl345: Controlling I/O with command %u\n", cmd);

    switch (cmd) {
    case ADXL345_SET_AXIS_X: /* Fallthrough */
    case ADXL345_SET_AXIS_Y: /* Fallthrough */
    case ADXL345_SET_AXIS_Z:
        adxl345dev->axis = cmd - ADXL345_SET_AXIS_X;
        break;
    default:
        return -ENOTTY;
    }

    return 0;
}

static struct i2c_device_id adxl345_idtable[] = {
    { "adxl345", 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, adxl345_idtable);

#ifdef CONFIG_OF
static const struct of_device_id adxl345_of_match[] = {
    { .compatible = "ad,adxl345", },
    { }
};

MODULE_DEVICE_TABLE(of, adxl345_of_match);
#endif

static struct i2c_driver adxl345_driver = {
    .driver = {
        .name   = NAME,
        .of_match_table = of_match_ptr(adxl345_of_match),
    },

    .id_table       = adxl345_idtable,
    .probe          = adxl345_probe,
    .remove         = adxl345_remove,
};

module_i2c_driver(adxl345_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Accelerometer driver for the Analog Devices ADXL345.");
MODULE_AUTHOR("Hovind");
