/*
 * mfhssdrv_ioctl.h
 *
 *  Created on: 06.11.2017
 *      Author: alex
 */

#ifndef MFHSSDRV_IOCTL_H_
#define MFHSSDRV_IOCTL_H_

#include <linux/types.h>
#include <linux/ioctl.h>

typedef struct
{
	/* const */ char regName[32];
	unsigned int address;
} __attribute__((__packed__)) MFHSS_REG_TypeDef;

typedef struct
{
	/* const */ char nodeName[32];
} MFHSS_GROUP_TypeDef;

/* Use 'm' as mfhssdrv magic number */
#define MFHSSDRV_IOC_MAGIC 'm'

/* SPIDRV commands */
#define MFHSSDRV_IORESET 		_IO(MFHSSDRV_IOC_MAGIC, 0)
#define MFHSSDRV_IOMAKEREG 		_IOW(MFHSSDRV_IOC_MAGIC, 1, MFHSS_REG_TypeDef)
#define MFHSSDRV_IOMAKEGROUP	_IOW(MFHSSDRV_IOC_MAGIC, 2, MFHSS_GROUP_TypeDef)

#define MFHSSDRV_IOC_MAXNR 3

#endif /* MFHSSDRV_IOCTL_H_ */
