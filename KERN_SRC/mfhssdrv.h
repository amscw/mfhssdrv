
#define DRIVER_NAME "mfhssdrv"
#define PDEBUG(fmt,args...) printk(KERN_DEBUG"%s(%s):"fmt, DRIVER_NAME, __FUNCTION__, ##args)
#define PERR(fmt,args...) printk(KERN_ERR"%s(%s):"fmt, DRIVER_NAME, __FUNCTION__, ##args)
#define PINFO(fmt,args...) printk(KERN_INFO"%s:"fmt,DRIVER_NAME, ##args)
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/fs.h>
#include<linux/init.h>
#include<linux/kdev_t.h>
#include<linux/module.h>
#include<linux/types.h>
#include <linux/kobject.h>
#include<linux/uaccess.h>
#include<linux/of.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
