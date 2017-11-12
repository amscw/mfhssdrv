/*
===============================================================================
Driver Name		:		mfhssdrv
Author			:		MOSKVIN
License			:		GPL
Description		:		LINUX DEVICE DRIVER PROJECT
===============================================================================
*/

#include"mfhssdrv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MOSKVIN");

/**
 * Драйвер параметризован регистровой картой модема.
 * Пример команды "пробы" модуля:
 * modprobe mfhssdrv regmap=$REGS regcount=$REGCOUNT
 * При успешной загрузке модуля все регистры модема, соответствующие указанной карте регистров будут отображены в sysfs.
 *
 * Независимо от переданной регистровой карты, всегда существует несколько hardcoded-регистров, которые используются
 * во всех операциях драйвера - чтение, запись и т.д.
 *
 * Если параметры модуля не были указаны, в sysfs отобразятся hardcoded-регистры.
 */

//-------------------------------------------------------------------------------------------------
// Prototypes
//-------------------------------------------------------------------------------------------------
static int mfhssdrv_probe(struct platform_device *pdev);
static int mfhssdrv_remove(struct platform_device *pdev);
static int mfhssdrv_open(struct inode *inode,struct file *filp);
static int mfhssdrv_release(struct inode *inode,struct file *filp);
static ssize_t mfhssdrv_read(struct file *filp, char __user *ubuff,size_t count,loff_t *offp);
static ssize_t mfhssdrv_write(struct file *filp, const char __user *ubuff, size_t count, loff_t *offp);
static ssize_t sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf);
static ssize_t sysfs_store(struct kobject *kobj, struct attribute* attr, const char *buf, size_t len);
static int mfhssdrv_proc_open(struct inode *inode, struct file *file);
static int mfhssdrv_proc_show(struct seq_file *m, void *v);
static char *mydevnode(struct device *dev, umode_t *mode);
static irqreturn_t mfhssdrv_irq_rx_handler(int irq, void *dev_id /*, struct pt_regs *regs */ );
static irqreturn_t mfhssdrv_irq_tx_handler(int irq, void *dev_id /*, struct pt_regs *regs */ );

//-------------------------------------------------------------------------------------------------
// Global variables
//-------------------------------------------------------------------------------------------------
static char *regmap = "";
static int regcount;
int mfhssdrv_major=0;
dev_t mfhssdrv_device_num;
atomic_t dev_cnt = ATOMIC_INIT(MFHSSDRV_FIRST_MINOR - 1);	// счетчик устройств - используется для нескольких минорных номеров
static struct class *device_class;

// операции символьного устройства
static const struct file_operations mfhssdrv_fops= {
	.owner				= THIS_MODULE,
	.open				= mfhssdrv_open,
	.release			= mfhssdrv_release,
	.read				= mfhssdrv_read,
	.write				= mfhssdrv_write,
};

// операции в procfs
static const struct file_operations proc_fops = {
	.owner = THIS_MODULE,
	.open = mfhssdrv_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

// операции в sysfs
static struct sysfs_ops modem_sysfs_ops = {
	.show = sysfs_show,
	.store = sysfs_store,
};

// тип объекта kobject, default_attrs заполняется в probe()
static struct kobj_type modem_sysfs_type = {
	.sysfs_ops = &modem_sysfs_ops,
};

// hardcoded attributes
static struct register_attribute register_dma_cr = {
	.default_attribute = {
		.name = "dma_cr",
		.mode = 0644,
	},
	.address = 0x0004,
	.value = 0,
	.priv = NULL,
};

static struct register_attribute register_dma_sr = {
	.default_attribute = {
		.name = "dma_sr",
		.mode = 0644,
	},
	.address = 0x0008,
	.value = 0,
	.priv = NULL,
};

static struct register_attribute register_dma_ir = {
	.default_attribute = {
		.name = "dma_ir",
		.mode = 0644,
	},
	.address = 0x000C,
	.value = 0,
	.priv = NULL,
};

static struct register_attribute register_dma_sa = {
	.default_attribute = {
		.name = "dma_sa",
		.mode = 0644,
	},
	.address = 0x0010,
	.value = 0,
	.priv = NULL,
};

static struct register_attribute register_dma_da = {
	.default_attribute = {
		.name = "dma_da",
		.mode = 0644,
	},
	.address = 0x0014,
	.value = 0,
	.priv = NULL,
};

static struct register_attribute register_dma_sl = {
	.default_attribute = {
		.name = "dma_sl",
		.mode = 0644,
	},
	.address = 0x0018,
	.value = 0,
	.priv = NULL,
};

static struct register_attribute register_dma_dl = {
	.default_attribute = {
		.name = "dma_dl",
		.mode = 0644,
	},
	.address = 0x001C,
	.value = 0,
	.priv = NULL,
};

static struct register_attribute register_mlip_sr = {
	.default_attribute = {
		.name = "mlip_sr",
		.mode = 0644,
	},
	.address = 0x0020,
	.value = 0,
	.priv = NULL,
};

static struct register_attribute register_mlip_ir = {
	.default_attribute = {
		.name = "mlip_ir",
		.mode = 0644,
	},
	.address = 0x0024,
	.value = 0,
	.priv = NULL,
};

static struct register_attribute register_mlip_rst = {
	.default_attribute = {
		.name = "mlip_rst",
		.mode = 0644,
	},
	.address = 0x0028,
	.value = 0,
	.priv = NULL,
};

static struct register_attribute register_mlip_ce = {
	.default_attribute = {
		.name = "mlip_ce",
		.mode = 0644,
	},
	.address = 0x002C,
	.value = 0,
	.priv = NULL,
};

static struct of_device_id mfhssdrv_of_match[] = {
	{ .compatible = "xlnx,axi-modem-fhss-1.0", },
	{ .compatible = "axi-modem-fhss-1.0", },
	{ },
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

//-------------------------------------------------------------------------------------------------
// MACRO
//-------------------------------------------------------------------------------------------------
#define WR_HARDCODED_REG(name, value) iowrite32(value, (void __iomem*)(name.priv->io_base + name.address))
#define RD_HARDCODED_REG(name) ioread32((void __iomem *)(name.priv->io_base + name.address))

//-------------------------------------------------------------------------------------------------
// Functions
//-------------------------------------------------------------------------------------------------
static int mfhssdrv_open(struct inode *inode,struct file *filp)
{
	/* TODO Auto-generated Function */
	mfhssdrv_private *charpriv = container_of(inode->i_cdev, mfhssdrv_private, cdev);
	struct platform_private *priv = container_of(charpriv, struct platform_private, charpriv);

	// PINFO("In char driver open() function\n");

	if (charpriv->device_open)
	{
		PERR("device already opened!\n");
		return -EBUSY;
	}

	if (request_irq(priv->irq_rx, mfhssdrv_irq_rx_handler, IRQF_SHARED, DRIVER_NAME, priv))
	{
		PERR("Failed to request for rx irq\n");
		return -ENOMEM;
	}

	if (request_irq(priv->irq_tx, mfhssdrv_irq_tx_handler, IRQF_SHARED, DRIVER_NAME, priv))
	{
		PERR("Failed to request for tx irq\n");
		return -ENOMEM;
	}

	filp->private_data = charpriv;
	charpriv->device_open++;
	return 0;
}					

static int mfhssdrv_release(struct inode *inode,struct file *filp)
{
	/* TODO Auto-generated Function */
	mfhssdrv_private *charpriv = filp->private_data;
	struct platform_private *priv = container_of(charpriv, struct platform_private, charpriv);

	// PINFO("In char driver release() function\n");
	free_irq(priv->irq_rx, priv);
	free_irq(priv->irq_tx, priv);
	charpriv->device_open--;
	return 0;
}

static ssize_t mfhssdrv_read(struct file *filp, char __user *ubuff,size_t count,loff_t *offp)
{
	/* TODO Auto-generated Function */
	int n = 0, res;
	mfhssdrv_private *charpriv = filp->private_data;

	// PINFO("In char driver read() function\n");

	res = wait_event_interruptible_timeout(charpriv->wq_rx, charpriv->flag_wait_rx != 0, msecs_to_jiffies(1));
	if (res == 0)
	{
		// @condition evaluated to %false after the @timeout elapsed
		return 0;
	}
	n = RD_HARDCODED_REG(register_dma_dl);
	copy_to_user(ubuff, charpriv->dst_addr, n);
	charpriv->flag_wait_rx = 0;
	return n;
}

static ssize_t mfhssdrv_write(struct file *filp, const char __user *ubuff, size_t count, loff_t *offp)
{
	/* TODO Auto-generated Function */
	int res;
	mfhssdrv_private *charpriv = filp->private_data;

	// PINFO("In char driver write() function\n");

	if (count > MFHSSDRV_DMA_SIZE)
		count = MFHSSDRV_DMA_SIZE;

	copy_from_user(charpriv->src_addr, ubuff, count);
	WR_HARDCODED_REG(register_dma_sl, count);
	WR_HARDCODED_REG(register_dma_cr, 1);
	res = wait_event_interruptible_timeout(charpriv->wq_tx, charpriv->flag_wait_tx != 0, msecs_to_jiffies(1000));
	if (res == 0)
	{
		// @condition evaluated to %false after the @timeout elapsed
		return 0;
	}
	charpriv->flag_wait_tx = 0;
	return count;
}

static ssize_t sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	unsigned long flags = 0;
	struct register_attribute *regattr = container_of(attr, struct register_attribute, default_attribute);

	spin_lock_irqsave(&regattr->priv->lock, flags);
	regattr->value = ioread32((void __iomem*)(regattr->priv->io_base + regattr->address));
	spin_unlock_irqrestore(&regattr->priv->lock, flags);
	PDEBUG("read from %s @ 0x%X = 0x%X\n", regattr->default_attribute.name, regattr->address, regattr->value);
	return scnprintf(buf, PAGE_SIZE, "%d\n", regattr->value);
}

static ssize_t sysfs_store(struct kobject *kobj, struct attribute* attr, const char *buf, size_t len)
{
	unsigned long flags = 0;
	struct register_attribute *regattr = container_of(attr, struct register_attribute, default_attribute);

	spin_lock_irqsave(&regattr->priv->lock, flags);
	sscanf(buf, "%d", &regattr->value);
	iowrite32(regattr->value, (void __iomem*)(regattr->priv->io_base + regattr->address));
	spin_unlock_irqrestore(&regattr->priv->lock, flags);
	PDEBUG("write 0x%X to %s @ 0x%X\n", regattr->value, regattr->default_attribute.name, regattr->address);
	return len;
}

static int mfhssdrv_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, mfhssdrv_proc_show, NULL);
}

static int mfhssdrv_proc_show(struct seq_file *m, void *v)
{
	char buffer[80];
	sprintf(buffer, "%s - %d\n", "major number", mfhssdrv_major);
	seq_printf(m, buffer);
	return 0;
}

static char *mydevnode(struct device *dev, umode_t *mode)
{
	if (mode) {
		*mode = 0644;
	}
	return 0;
}

static irqreturn_t mfhssdrv_irq_rx_handler(int irq, void *dev_id /*, struct pt_regs *regs */ )
{
	struct platform_private *priv = (struct platform_private*)dev_id;

	WR_HARDCODED_REG(register_dma_sr, 2);
	priv->charpriv.flag_wait_rx = 1;
	wake_up_interruptible(&priv->charpriv.wq_rx);
	return IRQ_HANDLED;
}

static irqreturn_t mfhssdrv_irq_tx_handler(int irq, void *dev_id /*, struct pt_regs *regs */ )
{
	struct platform_private *priv = (struct platform_private*)dev_id;

	WR_HARDCODED_REG(register_mlip_sr, 1);
	priv->charpriv.flag_wait_tx = 1;
	wake_up_interruptible(&priv->charpriv.wq_tx);
	return IRQ_HANDLED;
}

static int mfhssdrv_probe(struct platform_device *pdev)
{
	int res, i;
	dev_t curr_dev;
	char *regentry;
	struct register_attribute *regattr = NULL;
	unsigned long memsize;
	unsigned int minor = atomic_inc_return(&dev_cnt);	// выполняет атомарный инкремент и возвращает инкрементированное значение
	struct platform_private *priv;
	mfhssdrv_private *charpriv;
	static struct attribute *default_attributes[] = {
		&register_dma_cr.default_attribute,
		&register_dma_sr.default_attribute,
		&register_dma_ir.default_attribute,
		&register_dma_sa.default_attribute,
		&register_dma_da.default_attribute,
		&register_dma_sl.default_attribute,
		&register_dma_dl.default_attribute,
		&register_mlip_sr.default_attribute,
		&register_mlip_ir.default_attribute,
		&register_mlip_rst.default_attribute,
		&register_mlip_ce.default_attribute,
		NULL
	};

	PINFO("In probe() function\n");

	if (minor == MFHSSDRV_N_MINORS + MFHSSDRV_FIRST_MINOR)
		return -EAGAIN;

	curr_dev = MKDEV(MAJOR(mfhssdrv_device_num), MINOR(mfhssdrv_device_num) + minor);
	PDEBUG("current device number: major = %d, minor = %d\n", MAJOR(curr_dev), MINOR(curr_dev));

	// 1. выделение памяти под структуру устройства
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		PERR("Failed to allocate memory for the private data structure\n");
		goto fail_alloc_priv;
	}
	charpriv = &priv->charpriv;
	charpriv->device_open = 0;	// понятно, что и так там 0, просто акцентирую внимание !

	// 2. создание файловой системы procfs
	charpriv->our_proc_file = proc_create(PROCFS_NAME, 0644, NULL, &proc_fops);
	if (!charpriv->our_proc_file)
	{
		PERR("Failed to create procfs node\n");
		goto fail_create_procfs;
	}
	device_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(device_class))
	{
		PERR("Failed to create device class\n");
		goto fail_create_device_class;
	}
	device_class->devnode = mydevnode;
	charpriv->device = device_create(device_class, NULL, curr_dev, NULL, DRIVER_NAME);
	if (IS_ERR(charpriv->device)) {
		PERR("Failed to create device\n");
		goto fail_create_device;
	}

	// 3. создание файловой системы sysfs
	charpriv->registers = kzalloc(sizeof *charpriv->registers, GFP_KERNEL);
	if (!charpriv->registers)
	{
		PERR("Failed to allocate memory for kobject\n");
		goto fail_alloc_kobj;
	}
	// container_of не нужен, так как оба указателя совпадают
	for (regattr = (struct register_attribute*)default_attributes[0], i = 0; regattr; regattr = (struct register_attribute*) default_attributes[++i])
		regattr->priv = charpriv;

	if (regcount > 0)
	{
		// выделение памяти под массив указателей на атрибуты kobject
		memsize = (regcount + 1) * sizeof *modem_sysfs_type.default_attrs;
		modem_sysfs_type.default_attrs = (struct attribute**)kzalloc(memsize, GFP_KERNEL);
		if (!modem_sysfs_type.default_attrs)
		{
			PERR("Failed to allocate memory for array of pointers to attributes\n");
			goto fail_alloc_pattrs;
		}
		// PDEBUG("modem_sysfs_type.default_attrs = %p, total - %lu\n", modem_sysfs_type.default_attrs, memsize);

		// выделение памяти под атрибуты, парсинг карты регистров и заполнение атрибутов
		memsize = sizeof(struct register_attribute); i = 0;
		while ((regentry = strsep(&regmap, ";")) != NULL && i < regcount)
		{
			if (!modem_sysfs_type.default_attrs[i])
			{
				modem_sysfs_type.default_attrs[i] = (struct attribute*) kzalloc(memsize, GFP_KERNEL);
				if (!modem_sysfs_type.default_attrs[i])
				{
					PERR("Failed to allocate memory for sysfs attribute\n");
					goto fail_alloc_attr;
				}
				// PDEBUG("modem_sysfs_type.default_attrs[%d] = %p, total - %lu\n", i, modem_sysfs_type.default_attrs[i], memsize);
				regattr = (struct register_attribute*)modem_sysfs_type.default_attrs[i];
			} // else память была распределена на предыдущей итерации regentry

			if ((res = sscanf(regentry, "%s @0x%x", regattr->name, &regattr->address)) != 2)
			{
				// запись описания регистра невалидна
				PERR("register entry: %s is not valid (retrived: %d params, need: 2)\n", regentry, res);
				PDEBUG("name = %s\n", regattr->name);
				PDEBUG("address = %x\n", regattr->address);
				continue;
			}
			regattr->default_attribute.name = regattr->name;
			regattr->default_attribute.mode = 0644;
			regattr->value = 0;
			regattr->priv = charpriv;
			i++;
		}
		if (i < regcount)
		{
			// найдено меньше регистров, чем заявлено
			PERR("valid registers found: %d, need: %d\n", i, regcount);
			if (i == 0)
				kfree(modem_sysfs_type.default_attrs[i]);
		} else if (regentry != NULL) {
			// регистров больше, чем заявлено
			PERR("more than %d registers found! Next: %s\n", regcount, regentry);
		} else {
			// ок
			PINFO("regmap parsed: ok\n");
		}
		modem_sysfs_type.default_attrs[i] = NULL;
	} else {
		modem_sysfs_type.default_attrs = default_attributes;
	}

	// 4. регистрация kobject
	kobject_init(charpriv->registers, &modem_sysfs_type);
	res = kobject_add(charpriv->registers, NULL, "%s", "mfhssdrv-registers");
	if (res != 0)
	{
		PERR("Failed to register kobject\n");
		kobject_put(charpriv->registers);
		charpriv->registers = NULL;
		goto fail_alloc_attr;
	}

	// 5. резервирование памяти устройства (ресурсы памяти)
	res = of_address_to_resource(pdev->dev.of_node, 0, &charpriv->resource);
	if (res!= 0)
	{
		PERR("Failed to retrive memory space resource\n");
		goto fail_resource_mem;
	}
	if (request_mem_region(charpriv->resource.start, resource_size(&charpriv->resource), DRIVER_NAME) == NULL)
	{
		PERR("Failed to request memory for device\n");
		goto fail_resource_mem;
	}
	charpriv->io_base = of_iomap(pdev->dev.of_node, 0);
	if (!charpriv->io_base)
	{
		PERR("Failed to mapping memory region\n");
		goto fail_map_mem;
	}

	// 6. настройка прерываний
	// TODO: контроль извлечения номеров прерываний
	priv->irq_rx = irq_of_parse_and_map(pdev->dev.of_node, 0);
	priv->irq_tx = irq_of_parse_and_map(pdev->dev.of_node, 1);
	PDEBUG("IRQ (rx) = %d, IRQ (tx) = %d\n", priv->irq_rx, priv->irq_tx);

	charpriv->flag_wait_rx = 0;
	charpriv->flag_wait_tx = 0;
	init_waitqueue_head(&charpriv->wq_rx);
	init_waitqueue_head(&charpriv->wq_tx);
	spin_lock_init(&charpriv->lock);

	// 7. настройка DMA
	charpriv->src_addr = dma_zalloc_coherent(NULL, MFHSSDRV_DMA_SIZE, &charpriv->src_handle, GFP_KERNEL);
	if (!charpriv->src_addr)
	{
		PERR("Failed to allocate memory for DMA out-buffer \n");
		goto fail_alloc_dma_src;
	}
	charpriv->dst_addr = dma_zalloc_coherent(NULL, MFHSSDRV_DMA_SIZE, &charpriv->dst_handle, GFP_KERNEL);
	if (!charpriv->dst_addr)
	{
		PERR("Failed to allocate memory for DMA in-buffer \n");
		goto fail_alloc_dma_dst;
	}

	// 8. настройка устройства по hardcoded атрибутам
	WR_HARDCODED_REG(register_dma_sa, 	charpriv->src_handle);
	WR_HARDCODED_REG(register_dma_da, 	charpriv->dst_handle);
	WR_HARDCODED_REG(register_mlip_rst, 1);
	WR_HARDCODED_REG(register_mlip_rst, 0);
	WR_HARDCODED_REG(register_mlip_ir, 	1);
	WR_HARDCODED_REG(register_dma_ir, 	2);
	WR_HARDCODED_REG(register_mlip_ce, 	1);

	// 9. регитрация символьного драйвера
	cdev_init(&priv->charpriv.cdev, &mfhssdrv_fops);
	cdev_add(&priv->charpriv.cdev, curr_dev, 1);

	// 10. прочее...
	platform_set_drvdata(pdev, priv);

	PINFO("device probed successfully!\n");

	return 0;

fail_alloc_dma_dst:
	dma_free_coherent(NULL, MFHSSDRV_DMA_SIZE, charpriv->src_addr, charpriv->src_handle);

fail_alloc_dma_src:
	iounmap(charpriv->io_base);

fail_map_mem:				// point 5. fails
	release_mem_region(charpriv->resource.start, resource_size(&charpriv->resource));

fail_resource_mem:			// point 5. fails
	kobject_put(charpriv->registers);
	charpriv->registers = NULL;

fail_alloc_attr:			// point 3. or 4. fails
	// некоторые атрибуты могли быть успешно созданы - их тоже удаляем
	for (i = 0; i < regcount; i++)
	{
		if (modem_sysfs_type.default_attrs[i] != 0)
			kfree(modem_sysfs_type.default_attrs[i]);
	}
	kfree(modem_sysfs_type.default_attrs);

fail_alloc_pattrs:			// point 3. fails
	if (charpriv->registers != NULL)
		kfree(charpriv->registers);

fail_alloc_kobj:			// point 3. fails
	device_destroy(device_class, curr_dev);

fail_create_device:			// point 2. fails
	class_destroy(device_class);

fail_create_device_class:	// point 2. fails
	remove_proc_entry(PROCFS_NAME, NULL);

fail_create_procfs:			// point 2. fails
	kfree(priv);

fail_alloc_priv: 			// point 1. fails
	return -ENOMEM;
}

static int mfhssdrv_remove(struct platform_device *pdev)
{
	int i;
	struct platform_private *priv = platform_get_drvdata(pdev);
	mfhssdrv_private *charpriv = &priv->charpriv;
	unsigned int minor = atomic_read(&dev_cnt);
	dev_t curr_dev;

	PINFO("In remove() function\n");

	atomic_dec(&dev_cnt);
	curr_dev = MKDEV(MAJOR(mfhssdrv_device_num), MINOR(mfhssdrv_device_num) + minor);
	PDEBUG("current device number: major = %d, minor = %d\n", MAJOR(curr_dev), MINOR(curr_dev));

	// разрегистрация символьного драйвера
	cdev_del(&priv->charpriv.cdev);

	// освобождение памяти DMA
	dma_free_coherent(NULL, MFHSSDRV_DMA_SIZE, charpriv->dst_addr, charpriv->dst_handle);
	dma_free_coherent(NULL, MFHSSDRV_DMA_SIZE, charpriv->src_addr, charpriv->src_handle);

	// снять отображение и отдать адресное пространство
	iounmap(charpriv->io_base);
	release_mem_region(charpriv->resource.start, resource_size(&charpriv->resource));

	// удалить каталог sysfs
	kobject_put(charpriv->registers);

	// удалить динамические атрибуты sysfs
	for (i = 0; i < regcount; i++)
	{
		if (modem_sysfs_type.default_attrs[i] != 0)
			kfree(modem_sysfs_type.default_attrs[i]);
	}
	kfree(modem_sysfs_type.default_attrs);

	// удалить каталог procfs
	device_destroy(device_class, curr_dev);
	class_destroy(device_class);
	remove_proc_entry(PROCFS_NAME, NULL);

	platform_set_drvdata(pdev, NULL);

	/* Free the device specific structure */
	kfree(priv);
	return 0;
}

static int __init mfhssdrv_init(void)
{
	/* TODO Auto-generated Function Stub */
	int res;

	// получить номера драйвера
	res = alloc_chrdev_region(&mfhssdrv_device_num,	MFHSSDRV_FIRST_MINOR, MFHSSDRV_N_MINORS, DRIVER_NAME);
	if(res < 0) {
		PERR("register device no failed\n");
		return -1;
	}
	mfhssdrv_major = MAJOR(mfhssdrv_device_num);

	// регистрация платформенного драйвера
	res = platform_driver_register(&mfhssdrv_driver);
	if (res) {
		PERR("Failed to register the platform driver\n");
		return res;
	}
	PINFO("INIT success!\n");
	return 0;
}

static void __exit mfhssdrv_exit(void)
{	
	/* TODO Auto-generated Function Stub */
	PINFO("EXIT\n");
	platform_driver_unregister(&mfhssdrv_driver);
	unregister_chrdev_region(mfhssdrv_device_num, MFHSSDRV_N_MINORS);
}

//-------------------------------------------------------------------------------------------------
// MACRO
//-------------------------------------------------------------------------------------------------
module_init(mfhssdrv_init);
module_exit(mfhssdrv_exit);

module_param(regmap, charp, S_IRUGO);
module_param(regcount, int, S_IRUGO);
