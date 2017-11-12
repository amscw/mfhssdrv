/*
===============================================================================
Driver Name		:		mfhssdrv
Author			:		MOSKVIN
License			:		GPL
Description		:		LINUX DEVICE DRIVER PROJECT
===============================================================================
*/

#include "mfhssdrv.h"
#include "mfhssdrv_ioctl.h"

//-------------------------------------------------------------------------------------------------
// MACRO
//-------------------------------------------------------------------------------------------------
#define MFHSSDRV_N_MINORS 1
#define MFHSSDRV_FIRST_MINOR 0
#define MFHSSDRV_NODE_NAME "mfhss"
#define MFHSSDRV_BUFF_SIZE 1024

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MOSKVIN");

//-------------------------------------------------------------------------------------------------
// Type declarations
//-------------------------------------------------------------------------------------------------
typedef struct privatedata {
	int nMinor;
	char buff[MFHSSDRV_BUFF_SIZE];
	struct cdev cdev;
	struct device *mfhssdrv_device;
	// for I/O operations
	void __iomem *io_base;
	spinlock_t lock;
	// sysfs directories
	struct kobject *hardcoded_regs;
	struct kset *dynamic_regs;
} mfhssdrv_private;

// файлы полей регистров
struct reg_attribute {
	struct attribute default_attribute;
	u32 value;
	u8 bitOffset;
	u8 bitSize;
} __attribute__((__packed__));

// объекты-регистры
struct reg_object {
	struct kobject kobj;
	u32 address;
	mfhssdrv_private *charpriv;
};

//-------------------------------------------------------------------------------------------------
// Prototypes
//-------------------------------------------------------------------------------------------------
static int mfhssdrv_open(struct inode *inode,struct file *filp);
static int mfhssdrv_release(struct inode *inode,struct file *filp);
static ssize_t mfhssdrv_read(struct file *filp,	char __user *ubuff,size_t count, loff_t *offp);
static ssize_t mfhssdrv_write(struct file *filp, const char __user *ubuff, size_t count, loff_t *offp);
static long mfhssdrv_ioctl(struct file *filp, unsigned int cmd , unsigned long arg);
static ssize_t sysfs_show_reg(struct kobject *kobj, struct attribute *attr, char *buf);
static ssize_t sysfs_store_reg(struct kobject *kobj, struct attribute* attr, const char *buf, size_t len);
static void release_reg(struct kobject *kobj);

//-------------------------------------------------------------------------------------------------
// Variables
//-------------------------------------------------------------------------------------------------
int mfhssdrv_major=0;
dev_t mfhssdrv_device_num;
struct class *mfhssdrv_class;
// TODO: спрятать в private data для platform device
mfhssdrv_private device;

static const struct file_operations mfhssdrv_fops= {
	.owner				= THIS_MODULE,
	.open				= mfhssdrv_open,
	.release			= mfhssdrv_release,
	.read				= mfhssdrv_read,
	.write				= mfhssdrv_write,
	.unlocked_ioctl		= mfhssdrv_ioctl,
};

// sysfs objects and attributes for dynamic registers
static struct reg_attribute reg_value = {
	{ .name = "value", .mode = S_IRUGO | S_IWUSR },
	0, 0, 0,
};

static struct attribute *default_reg_attrs[] = {
	&reg_value.default_attribute,
	NULL
};

static struct sysfs_ops reg_ops = {
	.show = sysfs_show_reg,
	.store = sysfs_store_reg,
};

static struct kobj_type reg_type = {
	.release = release_reg,
	.sysfs_ops = &reg_ops,
	.default_attrs = default_reg_attrs,
};

//-------------------------------------------------------------------------------------------------
// Functions
//-------------------------------------------------------------------------------------------------
static void release_reg(struct kobject *kobj)
{
	PDEBUG("release not implemented\n");
}

static ssize_t sysfs_show_reg(struct kobject *kobj, struct attribute *attr, char *buf)
{
	PDEBUG("sysfs_show not implemented\n");
	return 0;
}

static ssize_t sysfs_store_reg(struct kobject *kobj, struct attribute* attr, const char *buf, size_t len)
{
	PDEBUG("sysfs_store not implemented\n");
	return 0;
}


static int mfhssdrv_open(struct inode *inode,struct file *filp)
{
	/* TODO Auto-generated Function */
	mfhssdrv_private *priv = container_of(inode->i_cdev, mfhssdrv_private, cdev);

	filp->private_data = priv;
	PINFO("In char driver open() function\n");
	return 0;
}					

static int mfhssdrv_release(struct inode *inode,struct file *filp)
{
	/* TODO Auto-generated Function */
	mfhssdrv_private *priv = filp->private_data;
	PINFO("In char driver release() function\n");
	return 0;
}

static ssize_t mfhssdrv_read(struct file *filp,	char __user *ubuff,size_t count, loff_t *offp)
{
	/* TODO Auto-generated Function */
	int n=0;
	mfhssdrv_private *priv = filp->private_data;

	PINFO("In char driver read() function\n");
	return n;
}

static ssize_t mfhssdrv_write(struct file *filp, const char __user *ubuff, size_t count, loff_t *offp)
{
	/* TODO Auto-generated Function */
	int n=0;
	mfhssdrv_private *priv = filp->private_data;

	PINFO("In char driver write() function\n");
	return n;
}

static long mfhssdrv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int res = 0;
	mfhssdrv_private *charpriv = filp->private_data;
	MFHSS_REG_TypeDef reg_descr;
	struct reg_object *reg;

	PINFO("In char driver ioctl() function\n");

	// validate type
	if (_IOC_TYPE(cmd) != MFHSSDRV_IOC_MAGIC)
		return -ENOTTY;
	// validate number
	if (_IOC_NR(cmd) > MFHSSDRV_IOC_MAXNR)
		return -ENOTTY;
	// validate access
	if (_IOC_DIR(cmd) & _IOC_READ)
		res = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		res = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (res)
		return -EFAULT;

	// can process
	switch (cmd)
	{
	case MFHSSDRV_IORESET:
		PDEBUG("mfhssdrv_ioctl: Performing reset\n");
		// TODO: MFHSSDRV_IORESET not implemented
		break;

	case MFHSSDRV_IOMAKEREG:
		// забираем описание регистра из пространства пользователя
		copy_from_user(&reg_descr, (const void __user *)arg, sizeof reg_descr);
		// выделяем память под новый регистр
		reg = kzalloc(sizeof *reg, GFP_KERNEL);
		if (!reg)
		{
			PERR("mfhssdrv_ioctl: Failed to alloc reg %s\n", reg_descr.name);
			return -ENOMEM;
		}
		// настраиваем его kobj и регистрируем
		kobject_init(&reg->kobj, &reg_type);
		reg->kobj.kset = charpriv->dynamic_regs;
		res = kobject_add(&reg->kobj, &charpriv->dynamic_regs->kobj, "%s", reg_descr.name);	// будем надеяться, что name будет скопирован.
		if (res != 0)
		{
			PERR("mfhssdrv_ioctl: Failed to register reg %s\n", reg_descr.name);
			kfree(reg); // FIXME: kobject_put(reg->kobj); // будет вызван release, который удалит reg
			return -ENOMEM;
		}
		reg->address = reg_descr.address;
		reg->charpriv = charpriv;
		PDEBUG("mfhssdrv_ioctl: new register added successfully (%s@0x%x)\n", reg_descr.name, reg_descr.address);
		break;

	default:
		return -ENOTTY;
	}

	return 0;
}


static int __init mfhssdrv_init(void)
{
	/* TODO Auto-generated Function Stub */
	int res;

	res = alloc_chrdev_region(&mfhssdrv_device_num,MFHSSDRV_FIRST_MINOR,MFHSSDRV_N_MINORS ,DRIVER_NAME);
	if(res) {
		PERR("register device no failed\n");
		return -1;
	}
	mfhssdrv_major = MAJOR(mfhssdrv_device_num);

	mfhssdrv_class = class_create(THIS_MODULE , DRIVER_NAME);
	if(!mfhssdrv_class) {
		PERR("class creation failed\n");
		return -1;
	}
	PINFO("INIT\n");

	// TODO: перенести в probe()
	mfhssdrv_device_num = MKDEV(mfhssdrv_major, MFHSSDRV_FIRST_MINOR);
	mfhssdrv_private *charpriv = &device;

	// контейнер для динамически создаваемых регистров
	charpriv->dynamic_regs = kset_create_and_add("mfhssdrv-dynamic", NULL, NULL);
	if (!charpriv->dynamic_regs)
	{
		PERR("Failure to create kset for dynamic objects\n");
		return -ENOMEM;
	}

	// регистрация устройства
	cdev_init(&charpriv->cdev , &mfhssdrv_fops);
	cdev_add(&charpriv->cdev, mfhssdrv_device_num, 1);
	charpriv->mfhssdrv_device = device_create(mfhssdrv_class, NULL, mfhssdrv_device_num, NULL, MFHSSDRV_NODE_NAME"%d", MFHSSDRV_FIRST_MINOR);
	charpriv->nMinor = MFHSSDRV_FIRST_MINOR;

	return 0;
}

static void __exit mfhssdrv_exit(void)
{	
	/* TODO Auto-generated Function Stub */
	// TODO: перенести в remove()
	mfhssdrv_private *charpriv = &device;
	kset_unregister(charpriv->dynamic_regs);
	mfhssdrv_device_num= MKDEV(mfhssdrv_major, MFHSSDRV_FIRST_MINOR);
	// unregister device
	cdev_del(&charpriv->cdev);
	device_destroy(mfhssdrv_class, mfhssdrv_device_num);

	PINFO("EXIT\n");

	class_destroy(mfhssdrv_class);
	unregister_chrdev_region(mfhssdrv_device_num ,MFHSSDRV_N_MINORS);	
}

module_init(mfhssdrv_init);
module_exit(mfhssdrv_exit);

