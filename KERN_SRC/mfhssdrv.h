#define DRIVER_NAME "mfhssdrv"
#define PDEBUG(fmt,args...) printk(KERN_DEBUG"%s:"fmt,DRIVER_NAME, ##args)
#define PERR(fmt,args...) printk(KERN_ERR"%s:"fmt,DRIVER_NAME,##args)
#define PINFO(fmt,args...) printk(KERN_INFO"%s:"fmt,DRIVER_NAME, ##args)
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/proc_fs.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>

//-------------------------------------------------------------------------------------------------
// MACROCONSTANTS
//-------------------------------------------------------------------------------------------------
#define MFHSSDRV_N_MINORS 		1
#define MFHSSDRV_FIRST_MINOR 	0
#define MFHSSDRV_DMA_SIZE 		1024
#define MFHSSDRV_PLATFORM_NAME 	DRIVER_NAME
#define PROCFS_NAME	"mfhssdrv-procfs"

//-------------------------------------------------------------------------------------------------
// Types declarations
//-------------------------------------------------------------------------------------------------
typedef struct privatedata {
	int nMinor;
	void __iomem *io_base;
	char *src_addr;
	char *dst_addr;
	dma_addr_t src_handle;
	dma_addr_t dst_handle;
	wait_queue_head_t wq_rx;
	wait_queue_head_t wq_tx;
	spinlock_t lock;
	int flag_wait_rx;
	int flag_wait_tx;
	int device_open;
	struct device *device;
	struct kobject *registers;
	struct resource resource;
	struct proc_dir_entry *our_proc_file;
	struct cdev cdev;
} mfhssdrv_private;

struct platform_private {
	struct platform_device *pdev;
	int irq_rx;
	int irq_tx;
	mfhssdrv_private charpriv;
};

struct register_attribute
{
	struct attribute default_attribute;
	char name[32];
	unsigned int address;
	int value;
	mfhssdrv_private *priv;
};
