/* SPDX-License-Identifier:     GPL-2.0+ */
/*
 * (C) Copyright 2022 Rockchip Electronics Co., Ltd
 *
 */

#ifndef __CONFIG_RK3562_COMMON_H
#define __CONFIG_RK3562_COMMON_H

#define CFG_CPUID_OFFSET		0xa

#include "rockchip-common.h"

#define CONFIG_SPL_FRAMEWORK
#define CONFIG_SPL_TEXT_BASE		0x00000000
#define CONFIG_SPL_MAX_SIZE		0x00040000
#define CONFIG_SPL_BSS_START_ADDR	0x03fe0000
#define CONFIG_SPL_BSS_MAX_SIZE		0x00010000
#define CONFIG_SPL_STACK		0x03fe0000

#define CONFIG_SYS_MALLOC_LEN		(32 << 20)
#define CONFIG_SYS_CBSIZE		1024
#define CONFIG_SKIP_LOWLEVEL_INIT

#ifdef CONFIG_SUPPORT_USBPLUG
#define CONFIG_SYS_TEXT_BASE		0x00000000
#else
#define CONFIG_SYS_TEXT_BASE		0x00200000
#endif

#define CONFIG_SYS_INIT_SP_ADDR		0x00c00000
#define CONFIG_SYS_LOAD_ADDR		0x00c00800
#define CONFIG_SYS_BOOTM_LEN		(64 << 20)	/* 64M */
#define COUNTER_FREQUENCY		24000000

#define GICD_BASE			0xfe901000
#define GICC_BASE			0xfe902000

/* secure otp */
#define OTP_UBOOT_ROLLBACK_OFFSET	0x350
#define OTP_UBOOT_ROLLBACK_WORDS	2	/* 64 bits, 2 words */
#define OTP_ALL_ONES_NUM_BITS		32
#define OTP_SECURE_BOOT_ENABLE_ADDR	0x20
#define OTP_SECURE_BOOT_ENABLE_SIZE	1
#define OTP_RSA4096_ENABLE_ADDR		0x21
#define OTP_RSA4096_ENABLE_SIZE		1
#define OTP_RSA_HASH_ADDR		0x180
#define OTP_RSA_HASH_SIZE		32

/* MMC/SD IP block */
#define CONFIG_BOUNCE_BUFFER

#define CONFIG_SYS_SDRAM_BASE		0
#define SDRAM_MAX_SIZE			0xfc000000
#define CONFIG_PREBOOT
#define CONFIG_SYS_NONCACHED_MEMORY	(1 << 20)	/* 1 MiB */

#define CONFIG_SPL_LOAD_FIT_ADDRESS	0x2000000

#ifndef CONFIG_SPL_BUILD
/* usb mass storage */
#define CONFIG_USB_FUNCTION_MASS_STORAGE
#define CONFIG_ROCKUSB_G_DNL_PID	0x350d

#define ENV_MEM_LAYOUT_SETTINGS \
	"scriptaddr=0x00c00000\0" \
	"pxefile_addr_r=0x00e00000\0" \
	"fdt_addr_r=0x08300000\0" \
	"kernel_addr_r=0x00400000\0" \
	"kernel_addr_c=0x04080000\0" \
	"ramdisk_addr_r=0x0a200000\0"

#include <config_distro_bootcmd.h>

#define CONFIG_EXTRA_ENV_SETTINGS \
	ENV_MEM_LAYOUT_SETTINGS \
	"partitions=" PARTS_RKIMG \
	ROCKCHIP_DEVICE_SETTINGS \
	RKIMG_DET_BOOTDEV \
	BOOTENV
#endif

/* rockchip ohci host driver */
#define CONFIG_USB_OHCI_NEW
#define CONFIG_SYS_USB_OHCI_MAX_ROOT_PORTS	1
#define CONFIG_LIB_HW_RAND

#endif
