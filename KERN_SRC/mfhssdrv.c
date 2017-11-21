/*
===============================================================================
Driver Name		:		mfhssdrv
Author			:		MOSKVIN
License			:		GPL
Description		:		LINUX DEVICE DRIVER PROJECT
===============================================================================
*/


// WARNING! kset никуда не можем встраивать, т.к. для него уже предопределена функция освобождения release и нет возможности её изменить

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
	struct kset *hardcoded_regs;
	struct kset *dynamic_regs;
} mfhssdrv_private;

typedef struct {
	mfhssdrv_private charpriv;
	struct platform_device *pdev;
	int irq_tx;
	int irq_rx;
} platform_private;

// файлы регистров
struct reg_attribute {
	struct attribute default_attribute;
	u32 address;
	u32 value;
} __attribute__((__packed__));

// каталоги регистров
struct reg_group {
	struct kobject kobj;
	mfhssdrv_private *charpriv;
};

//-------------------------------------------------------------------------------------------------
// MACRO (registers)
//-------------------------------------------------------------------------------------------------
// hardcoded registers
#define REG_DMA_CR_NAME			"cr"
#define REG_DMA_CR_ADDRESS		0x0004
#define REG_DMA_SR_NAME			"sr"
#define REG_DMA_SR_ADDRESS		0x0008
#define REG_DMA_IR_NAME			"ir"
#define REG_DMA_IR_ADDRESS		0x000C
#define REG_DMA_SA_NAME			"sa"
#define REG_DMA_SA_ADDRESS		0x0010
#define REG_DMA_DA_NAME			"da"
#define REG_DMA_DA_ADDRESS		0x0014
#define REG_DMA_SL_NAME			"sl"
#define REG_DMA_SL_ADDRESS		0x0018
#define REG_DMA_DL_NAME			"dl"
#define REG_DMA_DL_ADDRESS		0x001C
#define REG_MLIP_SR_NAME		"sr"
#define REG_MLIP_SR_ADDRESS		0x0020
#define REG_MLIP_IR_NAME		"ir"
#define REG_MLIP_IR_ADDRESS		0x0024
#define REG_MLIP_RST_NAME		"rst"
#define REG_MLIP_RST_ADDRESS	0x0028
#define REG_MLIP_CE_NAME		"ce"
#define REG_MLIP_CE_ADDRESS		0x001C

// имя переменной-регистра (атрибута)
#define REG(group, reg) reg_##group##_##reg

// имя переменной-группы
#define GROUP_TYPE(group) group##_type

// создает регистр (атрибут sysfs)
#define MAKE_REG(group, reg) \
	static struct reg_attribute REG(group, reg) = {\
		{\
			.name = REG_##group##_##reg##_NAME,\
			.mode = S_IRUGO | S_IWUSR\
		}, REG_##group##_##reg##_ADDRESS, 0\
}

// создает объект с операциями над регистром (sysfs_ops)
#define MAKE_GROUP_OPS(group) \
	static void release_##group(struct kobject *kobj)\
	{\
		struct reg_group *g = container_of(kobj, struct reg_group, kobj);\
		PDEBUG("destroing object: %s\n", kobj->name);\
		kfree(g);\
	}\
	\
	static ssize_t sysfs_show_##group(struct kobject *kobj, struct attribute *attr, char *buf)\
	{\
		unsigned long flags = 0;\
		struct reg_group *g = container_of(kobj, struct reg_group, kobj);\
		struct reg_attribute *a = container_of(attr, struct reg_attribute, default_attribute);\
		spin_lock_irqsave(&g->charpriv->lock, flags);\
		a->value = ioread32((void __iomem*)(g->charpriv->io_base + a->address));\
		spin_unlock_irqrestore(&g->charpriv->lock, flags);\
		PDEBUG("read from %s@0x%X = 0x%X\n", attr->name, a->address, a->value);\
		return scnprintf(buf, PAGE_SIZE, "%d\n", a->value);\
	}\
	\
	static ssize_t sysfs_store_##group(struct kobject *kobj, struct attribute* attr, const char *buf, size_t len)\
	{\
		unsigned long flags = 0;\
		struct reg_group *g = container_of(kobj, struct reg_group, kobj);\
		struct reg_attribute *a = container_of(attr, struct reg_attribute, default_attribute);\
		sscanf(buf, "%d", &a->value);\
		spin_lock_irqsave(&g->charpriv->lock, flags);\
		iowrite32(a->value, (void __iomem*)(g->charpriv->io_base + a->address));\
		spin_unlock_irqrestore(&g->charpriv->lock, flags);\
		PDEBUG("write 0x%X to %s@0x%X\n", a->value, a->default_attribute.name, a->address);\
		return len;\
	}\
	\
	static struct sysfs_ops group##_ops = {\
		.show = sysfs_show_##group,\
		.store = sysfs_store_##group,\
	};\

// создает тип регистра (тип kobject)
#define MAKE_GROUP_TYPE(group) \
	MAKE_GROUP_OPS(group);\
	static struct kobj_type GROUP_TYPE(group) = {\
		.sysfs_ops = &group##_ops,\
		.default_attrs = group##_attributes,\
		.release = release_##group\
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
static int mfhssdrv_probe(struct platform_device *pdev);
static int mfhssdrv_remove(struct platform_device *pdev);

//-------------------------------------------------------------------------------------------------
// Variables
//-------------------------------------------------------------------------------------------------
int mfhssdrv_major=0;
dev_t mfhssdrv_device_num;
struct class *mfhssdrv_class;
// TODO: спрятать в private data для platform device

static struct of_device_id mfhssdrv_of_match[] = {
	{ .compatible = "xlnx,axi-modem-fhss-1.0", },
	{}
};
MODULE_DEVICE_TABLE(of, mfhssdrv_of_match);

struct platform_driver mfhssdrv_driver = {
		.driver = {
			.name	= DRIVER_NAME,
			.owner	= THIS_MODULE,
			.of_match_table = of_match_ptr(mfhssdrv_of_match),
		},
		.probe 		= mfhssdrv_probe,
		.remove		= mfhssdrv_remove,
};


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
	0, 0
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

// hardcoded DMA registers
MAKE_REG(DMA, CR);
MAKE_REG(DMA, SR);
MAKE_REG(DMA, IR);
MAKE_REG(DMA, SA);
MAKE_REG(DMA, DA);
MAKE_REG(DMA, SL);
MAKE_REG(DMA, DL);
static struct attribute *DMA_attributes[] = {
	&REG(DMA, CR).default_attribute,
	&REG(DMA, SR).default_attribute,
	&REG(DMA, IR).default_attribute,
	&REG(DMA, SA).default_attribute,
	&REG(DMA, DA).default_attribute,
	&REG(DMA, SL).default_attribute,
	&REG(DMA, DL).default_attribute,
	NULL
};
MAKE_GROUP_TYPE(DMA);

// hardcoded MLIP registers
MAKE_REG(MLIP, SR);
MAKE_REG(MLIP, IR);
MAKE_REG(MLIP, RST);
MAKE_REG(MLIP, CE);
static struct attribute *MLIP_attributes[] = {
	&REG(MLIP, SR).default_attribute,
	&REG(MLIP, IR).default_attribute,
	&REG(MLIP, RST).default_attribute,
	&REG(MLIP, CE).default_attribute,
	NULL
};
MAKE_GROUP_TYPE(MLIP);

//-------------------------------------------------------------------------------------------------
// Functions
//-------------------------------------------------------------------------------------------------
/** container_of(ptr, type, member) :
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 */

static inline void destroy_objects(struct kset *pkset)
{
	struct list_head *pnext, *pcurr, *phead;
	struct kobject *pkobj = 0;

	if (!pkset)
		return;

	PDEBUG("destroing group: %s\n", pkset->kobj.name);


	// если был добавлен хотя бы один объект
	if (&pkset->list != pkset->list.next)
	{
		// удаление всех объектов множества
		for (phead = &pkset->list, pcurr = phead->next, pnext = 0; pnext != phead;  pcurr = pnext)
		{
			pnext = pcurr->next;	// запоминаем указатель на следующий объект ДО уничтожения текущего
			pkobj = list_entry(pcurr, struct kobject, entry);
			kobject_del(pkobj);
			kobject_put(pkobj);
		}
	}
	// удаление каталога верхнего уровня
	kset_unregister(pkset);
}

static inline void cleanup_all(mfhssdrv_private *charpriv)
{
	platform_private *priv = container_of(charpriv, platform_private, charpriv);

	if (!charpriv)
		return;

	destroy_objects(charpriv->dynamic_regs);
	destroy_objects(charpriv->hardcoded_regs);

	kfree(priv);
}

static void release_reg(struct kobject *kobj)
{
	struct reg_group *g = container_of(kobj, struct reg_group, kobj);
	PDEBUG("destroing object: %s\n", kobj->name);
	kfree(g);
}

static ssize_t sysfs_show_reg(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct kset *dynamic_regs = container_of(kobj->parent, struct kset, kobj);
	mfhssdrv_private *charpriv = container_of(&dynamic_regs, mfhssdrv_private, dynamic_regs);
	PDEBUG("charpriv address: %p\n", charpriv);
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
	struct reg_group *g;
	MFHSS_REG_TypeDef reg_descr;
	MFHSS_GROUP_TypeDef group_descr;

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
		PDEBUG("Performing reset\n");
		// TODO: MFHSSDRV_IORESET not implemented
		break;

	case MFHSSDRV_IOMAKEGROUP:
		// забираем описание группы
		copy_from_user(&group_descr, (const void __user *)arg, sizeof group_descr);
		// выделяем память под новый объект
		g = kzalloc(sizeof *g, GFP_KERNEL);
		if (!g)
		{
			PERR("Failed to alloc group object %s\n", group_descr.nodeName);
			return -ENOMEM;
		}
		// настраиваем его и регистрируем
		kobject_init(&g->kobj, &reg_type);
		g->kobj.kset = charpriv->dynamic_regs;
		res = kobject_add(&g->kobj, &charpriv->dynamic_regs->kobj, "%s", group_descr.nodeName);	// будем надеяться, что name будет скопирован.
		if (res != 0)
		{
			PERR("Failed to register group object %s\n", group_descr.nodeName);
			kobject_put(&g->kobj); // будет вызван release, который удалит reg
			return -ENOMEM;
		}
		PDEBUG("New group added successfully (%s)\n", group_descr.nodeName);
		break;

	case MFHSSDRV_IOMAKEREG:
		// забираем описание регистра из пространства пользователя
		copy_from_user(&reg_descr, (const void __user *)arg, sizeof reg_descr);
		//reg->address = reg_descr.address;
		//reg->charpriv = charpriv;

		break;

	default:
		PERR("unsupported command: 0x%x\n", cmd);
		return -ENOTTY;
	}

	return 0;
}

static int mfhssdrv_probe(struct platform_device *pdev)
{
	int res;
	struct reg_group *g;
	platform_private *priv;
	mfhssdrv_private *charpriv;

	// TODO: проверить факт повторного зондирования return -EAGAIN;

	// выделение памяти под структуру устройства и частичное её заполнение
	priv = kzalloc(sizeof *priv, GFP_KERNEL);
	if (!priv) {
		PERR("Failed to allocate memory for the private data structure\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, priv);
	charpriv = &priv->charpriv;
	charpriv->nMinor = MFHSSDRV_FIRST_MINOR;

	// контейнер для динамически создаваемых регистров
	charpriv->dynamic_regs = kset_create_and_add("mfhss-dynamic", NULL, NULL);
	if (!charpriv->dynamic_regs)
	{
		PERR("Failure to create kset for dynamic objects\n");
		cleanup_all(charpriv);
		return -ENOMEM;
	}

	// контейнер для статически создаваемых регистров
	charpriv->hardcoded_regs = kset_create_and_add("mfhss-hardcoded", NULL, NULL);
	if (!charpriv->hardcoded_regs)
	{
		PERR("Failure to create kset for hardcoded objects\n");
		cleanup_all(charpriv);
		return -ENOMEM;
	}

	// создаем подкаталоги для статически создаваемых регистров
	// группа регистров DMA
	g = kzalloc(sizeof *g, GFP_KERNEL);
	if (!g)
	{
		PERR("Failed to alloc group object\n");
		cleanup_all(charpriv);
		return -ENOMEM;
	}
	// настраиваем её и регистрируем
	kobject_init(&g->kobj, &GROUP_TYPE(DMA));
	g->kobj.kset = charpriv->hardcoded_regs;
	res = kobject_add(&g->kobj, &charpriv->hardcoded_regs->kobj, "%s", "dma");
	if (res != 0)
	{
		PERR("Failed to register group object\n");
		cleanup_all(charpriv);
		kobject_put(&g->kobj);
		return -ENOMEM;
	} else {
		g->charpriv = charpriv;
	}

	// группа регистров MLIP
	g = kzalloc(sizeof *g, GFP_KERNEL);
	if (!g)
	{
		PERR("Failed to alloc group object\n");
		cleanup_all(charpriv);
		return -ENOMEM;
	}
	// настраиваем её и регистрируем
	kobject_init(&g->kobj, &GROUP_TYPE(MLIP));
	g->kobj.kset = charpriv->hardcoded_regs;
	res = kobject_add(&g->kobj, &charpriv->hardcoded_regs->kobj, "%s", "mlip");
	if (res != 0)
	{
		PERR("Failed to register group object\n");
		cleanup_all(charpriv);
		kobject_put(&g->kobj);
		return -ENOMEM;
	} else {
		g->charpriv = charpriv;
	}

	// создать узел в /dev
	charpriv->mfhssdrv_device = device_create(mfhssdrv_class, NULL, mfhssdrv_device_num, NULL, MFHSSDRV_NODE_NAME"%d", MFHSSDRV_FIRST_MINOR);

	// регистрация устройства
	cdev_init(&charpriv->cdev , &mfhssdrv_fops);
	cdev_add(&charpriv->cdev, mfhssdrv_device_num, 1);



	PINFO("device probed successfully!\n");

	return 0;
}

// TODO: возможно, имеет смысл вынести все в exit, т.к. для SoC этот метод не будет вызван никогда
static int mfhssdrv_remove(struct platform_device *pdev)
{
	platform_private *priv = platform_get_drvdata(pdev);
	mfhssdrv_private *charpriv = &priv->charpriv;

	PINFO("In remove() function\n");

	// разрегистрация устройства
	cdev_del(&charpriv->cdev);

	// удаление узла из /dev
	device_destroy(mfhssdrv_class, mfhssdrv_device_num);

	// удалить все каталоги статических и динамических регистров
	cleanup_all(charpriv);

	// удаление структуры данных драйвера
	kfree(priv);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int __init mfhssdrv_init(void)
{
	int res;

	res = alloc_chrdev_region(&mfhssdrv_device_num, MFHSSDRV_FIRST_MINOR, MFHSSDRV_N_MINORS, DRIVER_NAME);
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

	res = platform_driver_register(&mfhssdrv_driver);
	if (res) {
		PERR("Failed to register the platform driver\n");
		return res;
	}
	PDEBUG("INIT ok!\n");

	return 0;
}

static void __exit mfhssdrv_exit(void)
{	
	class_destroy(mfhssdrv_class);
	unregister_chrdev_region(mfhssdrv_device_num, MFHSSDRV_N_MINORS);

	PDEBUG("EXIT ok!\n");

}

module_init(mfhssdrv_init);
module_exit(mfhssdrv_exit);
