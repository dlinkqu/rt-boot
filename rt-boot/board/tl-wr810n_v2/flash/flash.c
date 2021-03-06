/*
 * Qualcomm/Atheros High-Speed UART driver
 *
 * Copyright (C) 2015 Piotr Dymacz <piotr@dymacz.pl>
 * Copyright (C) 2014 Mantas Pucka <mantas@8devices.com>
 * Copyright (C) 2008-2010 Atheros Communications Inc.
 *
 * Values for UART_SCALE and UART_STEP:
 * https://www.mail-archive.com/openwrt-devel@lists.openwrt.org/msg22371.html
 *
 * Partially based on:
 * Linux/drivers/tty/serial/qca953x_uart.c
 *
 * SPDX-License-Identifier:GPL-2.0
 */

#include <kernel/rtthread.h>
#include <kernel/rthw.h>
#include <drivers/rtdevice.h>
#include <drivers/drivers/spi_flash.h>
#include <drivers/drivers/spi_flash_sfud.h>
#include <dfs/dfs_posix.h>

#include <global/global.h>
#include <soc/qca953x/qca953x_map.h>

#define SPI_BUS_NAME                "SPI0"
#define SPI_FLASH_DEVICE_NAME       "SPI00"
#define SPI_FLASH_CHIP              "SPI-FLASH0"

static struct rt_spi_device spi_flash_device;
static rt_spi_flash_device_t flash_chip_device;
static const sfud_flash *sfud_dev;

ALIGN(RT_ALIGN_SIZE)
static char board_flash_thread_stack[0x1000];
struct rt_thread board_flash_thread;
static struct rt_event board_flash_event;
static rt_int32_t board_flash_status;

#define BOARD_FLASH_EVENT_FIRMWARE 0
#define BOARD_FLASH_EVENT_UBOOT 0
#define BOARD_FLASH_EVENT_ART 0
#define BOARD_FLASH_EVENT_ALL 0

rt_int32_t board_flash_get_status(void)
{
	return board_flash_status;
}

void board_flash_firmware_notisfy(void)
{
	board_flash_status = 0;
	rt_event_send(&board_flash_event, (1 << BOARD_FLASH_EVENT_FIRMWARE));
}

static void board_flash_thread_entry(void* parameter)
{
	rt_uint32_t board_flash_event_flag;
	int flash_size;
	
	int firmware_start = 0;
	int firmware_length = 0;
    //int uboot_start = 0;
    //int uboot_length = 0;
    //int art_start = 0;
    //int art_length = 0;
    //int full_start = 0;
    //int full_length = 0;
	
	if (sfud_dev == RT_NULL)
	{
		board_flash_status = -1;
		return;
	}
	
	flash_size = sfud_dev->chip.capacity / 1024 / 1024;
	
	switch(flash_size)
	{
		case 8:
			firmware_start = 0x20000;
			firmware_length = 0x7C0000;
            //uboot_start = 0x00000;
            //uboot_length = 0x20000;
            //art_start = 0x7E0000;
            //art_length = 0x20000;
            //full_start = 0x0;
            //full_length = 0x800000;
			break;
		default:
			return;
	}
	while(1)
	{
		rt_event_recv(&board_flash_event, 
					  (1 << BOARD_FLASH_EVENT_FIRMWARE) |
					  (1 << BOARD_FLASH_EVENT_UBOOT) |
					  (1 << BOARD_FLASH_EVENT_ART) | 
					  (1 << BOARD_FLASH_EVENT_ALL) 
					  ,RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
					  RT_WAITING_FOREVER, &board_flash_event_flag);
		if(board_flash_event_flag & (1 << BOARD_FLASH_EVENT_FIRMWARE))
		{
			rt_uint8_t *buffer;
			int fd;
			int read_length;
			int write_off = firmware_start;
			board_flash_status = 0;
			buffer = rt_malloc(0x1000);
			if(buffer)
			{
				fd = open("/ram/firmware.bin", 0, O_RDONLY);
				if(fd < 0)
				{
					board_flash_status = -1;
				}
				else
				{
					sfud_erase(sfud_dev, firmware_start, firmware_length);
					
					while (1)
    				{
        				read_length = read(fd, buffer, 0x1000);
						
						if(read_length <= 0) break;
						
						sfud_write(sfud_dev,write_off,read_length,buffer);
						write_off+=read_length;
						
						board_flash_status = (write_off - firmware_start)*100 / firmware_length;
    				}
					board_flash_status = 100;
					close(fd);
				}
				
				rt_free(buffer);
			}
		}
		
	}
}

static void board_flash_thread_init(void)
{
	rt_event_init(&board_flash_event, "board flash event", RT_IPC_FLAG_FIFO);
    rt_thread_init(&board_flash_thread,
                   "board_flash_thread",
                   &board_flash_thread_entry,
                   RT_NULL,
                   &board_flash_thread_stack[0],
                   sizeof(board_flash_thread_stack),9,6);

    rt_thread_startup(&board_flash_thread);
}

static rt_err_t rt_hw_spi_flash_attach(void)
{
    /* attach cs */
	rt_err_t result;

    result = rt_spi_bus_attach_device(&spi_flash_device, SPI_FLASH_DEVICE_NAME, SPI_BUS_NAME, (void*)0);
	if (result != RT_EOK)
	{
		return result;
	}

	return RT_EOK;
}

rt_err_t soc_flash_init(void)
{
	rt_err_t result;
	result = rt_hw_spi_flash_attach();
	if (result != RT_EOK)
	{
		return result;
	}
    flash_chip_device = rt_sfud_flash_probe(SPI_FLASH_CHIP, SPI_FLASH_DEVICE_NAME);
    if (flash_chip_device == RT_NULL)
		return RT_ERROR;
	sfud_dev = (sfud_flash_t)flash_chip_device->user_data;
	rt_kprintf("SPI-FLASH:%dMB\n",sfud_dev->chip.capacity / 1024 / 1024);
	board_flash_thread_init();
	return RT_EOK;
}
