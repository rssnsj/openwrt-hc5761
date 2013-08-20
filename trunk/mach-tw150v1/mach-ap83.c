/*
 *  Atheros AP83 board support
 *
 *  Copyright (C) 2008-2012 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/spi/vsc7385.h>
#include <linux/gpio.h>

#include <asm/mach-ath79/ar71xx_regs.h>
#include <asm/mach-ath79/ath79.h>

#include "dev-eth.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-usb.h"
#include "dev-wmac.h"
#include "dev-m25p80.h"
#include "dev-ap9x-pci.h"
#include "machtypes.h"

static struct mtd_partition tw150v1_partitions[] = {
	{
		.name		= "u-boot",
		.offset		= 0,
		.size		= 0x010000,
	}, {
		.name		= "bdinfo",
		.offset		= 0x010000,
		.size		= 0x010000,
	}, {
		.name		= "kernel",
		.offset		= 0x020000,
		.size		= 0x140000,
	}, {
		.name		= "rootfs",
		.offset		= 0x160000,
		.size		= 0xe80000,
	}, {
		.name		= "nvram",
		.offset		= 0xfe0000,
		.size		= 0x010000,
	}, {
		.name		= "art",
		.offset		= 0xff0000,
		.size		= 0x010000,
	}, {
		.name		= "firmware",
		.offset		= 0x020000,
		.size		= 0xfc0000,
	}
};

static struct flash_platform_data tw150v1_flash_data = {
	.parts		= tw150v1_partitions,
	.nr_parts	= ARRAY_SIZE(tw150v1_partitions),
};

static struct gpio_led tw150v1_leds_gpio[] __initdata = {
	{
		.name		= "tw150v1:green:system",
		.gpio		= 1,   /* led1 */
		.active_low = 1,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "tw150v1:green:internet",
		.gpio		= 27,  /* led7 */
		.active_low = 1,
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	}, {
		.name		= "tw150v1:green:wlan-2p4",
		.gpio		= 0,   /* led0 */
		.active_low = 1,
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
};

/**
 * Button definition just as a sample here.
 */

#define AP83_KEYS_POLL_INTERVAL		20	/* msecs */
#define AP83_KEYS_DEBOUNCE_INTERVAL	(3 * AP83_KEYS_POLL_INTERVAL)

static struct gpio_keys_button tw150v1_gpio_keys[] __initdata = {
	{
		.desc		= "soft_reset",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = AP83_KEYS_DEBOUNCE_INTERVAL,
		.gpio		=  21 /* AP83_GPIO_BTN_RESET */,
		.active_low	= 1,
	}, {
		.desc		= "jumpstart",
		.type		= EV_KEY,
		.code		= KEY_WPS_BUTTON,
		.debounce_interval = AP83_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= 12, /* AP83_GPIO_BTN_JUMPSTART */
		.active_low	= 1,
	}
};

/* memmem(): A strstr() work-alike for non-text buffers */
static inline void *memmem(const void *s1, const void *s2, size_t len1, size_t len2)
{
	char *bf = (char *)s1, *pt = (char *)s2;
	size_t i, j;

	if (len2 > len1)
		return NULL;

	for (i = 0; i <= (len1 - len2); ++i) {
		for (j = 0; j < len2; ++j)
			if (pt[j] != bf[i + j]) break;
		if (j == len2) return (bf + i);
	}
	return NULL;
}

/**
 * There's a string in 'bdinfo' partition like
 *  "fac_mac = D4:EE:07:54:C2:8C" which indicates the router's
 *  base MAC address. We need to fetch it and set address to
 *  interfaces with it.
 */
static u8 __init *get_mac_from_bdinfo(u8 *mac, void *bdinfo, size_t info_sz)
{
	const char mac_key[] = "fac_mac = ";
	size_t mac_key_len = strlen(mac_key);
	void *mac_sp = NULL;
	unsigned int mac_ints[6];
	int i;

	if (!(mac_sp = memmem(bdinfo, mac_key, info_sz, mac_key_len))) {
		printk(KERN_ERR "%s: Cannot find MAC address prefix string '%s'.\n",
				__FUNCTION__, mac_key);
		return NULL;
	}
	mac_sp += mac_key_len;

	if (sscanf(mac_sp, "%2x:%2x:%2x:%2x:%2x:%2x", &mac_ints[0],
		&mac_ints[1], &mac_ints[2], &mac_ints[3], &mac_ints[4],
		&mac_ints[5]) != 6) {
		printk(KERN_ERR "%s: Cannot get correct MAC address.\n", __FUNCTION__);
		return NULL;
	}

	for (i = 0; i < 6; i++)
		mac[i] = (u8)mac_ints[i];

	return mac;
}

static void __init tw150v1_setup(void)
{
	u8 mac[6] = { 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a, };
	u8 *ee = (u8 *) KSEG1ADDR(0x1fff1000);
	
	get_mac_from_bdinfo(mac, (void *)KSEG1ADDR(0x1f010000), 0x200);

	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 1);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_RGMII;
	ath79_eth0_data.phy_mask = 0x1;

	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 0);
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_RGMII;
	ath79_eth1_data.speed = SPEED_1000;
	ath79_eth1_data.duplex = DUPLEX_FULL;

	ath79_eth1_pll_data.pll_1000 = 0x1f000000;

	ath79_register_mdio(0, 0x0);

	/* LAN ports */
	ath79_register_eth(1);

	/* WAN port */
	ath79_register_eth(0);

	ath79_register_m25p80(&tw150v1_flash_data);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(tw150v1_leds_gpio),
					tw150v1_leds_gpio);

	gpio_request_one(20, GPIOF_OUT_INIT_HIGH | GPIOF_EXPORT_DIR_FIXED, "gpio20");

	ath79_register_gpio_keys_polled(-1, AP83_KEYS_POLL_INTERVAL,
					 ARRAY_SIZE(tw150v1_gpio_keys),
					 tw150v1_gpio_keys);

	ath79_register_usb();

	ath79_register_wmac(ee, mac);
}

MIPS_MACHINE(ATH79_MACH_AP83, "tw150v1", "HiWiFi Router v1 (Atheros AR9330)", tw150v1_setup);
