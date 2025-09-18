/*
 * RPROM firmware
 *
 * Copyright (C) 2025 Niklas Ekström
 */
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"

#include "pico/flash.h"
#include "pico/multicore.h"

#include <string.h>

#include "protocol.h"

// Pins 0..15 are 16 bit data bus
// Pins 16..33 are 18 bit address bus
#define BYTE_PIN    34
#define CE_PIN      35
#define OE_PIN      36
#define RESET_PIN   37

#define DATA_MASK ((1 << 16) - 1)
#define ADDR_MASK ((1 << 18) - 1)

#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define PATCH_VERSION 0

#define ROM_SLOT_SIZE (512 * 1024)

__attribute__((section(".rom_image")))
static uint16_t rom_image[ROM_SLOT_SIZE / 2];

#define CONFIG_SECTOR_OFFSET    (ROM_SLOT_SIZE - FLASH_SECTOR_SIZE)
#define CONFIG_SECTOR_BASE      (XIP_BASE + CONFIG_SECTOR_OFFSET)

struct ConfigTuple
{
    uint32_t rom_slot;
    uint32_t reserved;
};

struct ConfigPage
{
    uint32_t pages_bitmap;
    uint32_t tuples_bitmap;
    struct ConfigTuple tuples[31];
};

static inline uint32_t get_active_rom_slot()
{
    struct ConfigPage *pages = (struct ConfigPage *)CONFIG_SECTOR_BASE;
    uint32_t active_page = __builtin_ctz(pages[0].pages_bitmap);
    uint32_t active_tuple = __builtin_ctz(pages[active_page].tuples_bitmap);
    uint32_t rom_slot = pages[active_page].tuples[active_tuple].rom_slot;
    return (rom_slot == 0 || rom_slot >= 8) ? 1 : rom_slot;
}

static void update_active_rom_slot_in_flash(uint32_t rom_slot)
{
    struct ConfigPage *pages = (struct ConfigPage *)CONFIG_SECTOR_BASE;
    uint32_t active_page = __builtin_ctz(pages[0].pages_bitmap);
    uint32_t active_tuple = __builtin_ctz(pages[active_page].tuples_bitmap);

    active_tuple++;
    if (active_tuple >= 31)
    {
        active_tuple = 0;
        active_page++;
        if (active_page >= 16)
        {
            active_page = 0;
            flash_range_erase(CONFIG_SECTOR_OFFSET, FLASH_SECTOR_SIZE);
        }
    }

    // Uses 256 bytes of stack space.
    struct ConfigPage page;
    memcpy(&page, (const void *)&pages[active_page], sizeof(page));

    page.pages_bitmap = ~((1 << active_page) - 1);
    page.tuples_bitmap = ~((1 << active_tuple) - 1);
    page.tuples[active_tuple].rom_slot = rom_slot;

    flash_range_program(CONFIG_SECTOR_OFFSET + active_page * FLASH_PAGE_SIZE,
                        (const uint8_t *)&page, FLASH_PAGE_SIZE);

    if (active_page != 0)
    {
        memcpy(&page, (const void *)&pages[0], FLASH_PAGE_SIZE);
        page.pages_bitmap = ~((1 << active_page) - 1);
        flash_range_program(CONFIG_SECTOR_OFFSET, (const uint8_t *)&page, FLASH_PAGE_SIZE);
    }
}

static void handle_magic_read(uint32_t address)
{
    uint32_t cmd = address >> 14;
    uint32_t arg = address & ((2 << 14) - 1);

    switch (cmd)
    {
        case CMD_UPDATE_ACTIVE_ROM_SLOT:
        {
            uint32_t rom_slot = arg;
            if (rom_slot == 0 || rom_slot > 7)
                return;

            update_active_rom_slot_in_flash(rom_slot);

            uint32_t rom_slot_base = XIP_BASE + rom_slot * ROM_SLOT_SIZE;
            memcpy(rom_image, (const void *)rom_slot_base, sizeof(rom_image));
            break;
        }
        case CMD_WRITE_STATUS_TO_SRAM:
        {
            struct StatusV1 *status = (struct StatusV1 *)rom_image;
            *status = (struct StatusV1) {
                .magic = STATUS_V1_MAGIC,
                .status_length = sizeof(struct StatusV1),
                .major_version = MAJOR_VERSION,
                .minor_version = MINOR_VERSION,
                .patch_version = PATCH_VERSION,
                .flash_size_mb = 4,
                .active_rom_slot = (uint8_t)get_active_rom_slot(),
            };
            break;
        }
        case CMD_RESTORE_PAGE_TO_SRAM:
        {
            const uint32_t rom_slot = get_active_rom_slot();
            const uint32_t rom_slot_base = XIP_BASE + rom_slot * ROM_SLOT_SIZE;
            memcpy(rom_image, (const void *)rom_slot_base, FLASH_PAGE_SIZE + 2);
            break;
        }
        case CMD_COPY_PAGE_AMIGA_TO_SRAM:
        {
            for (int i = 0; i < 128; i++)
                rom_image[i] = __builtin_bswap16((uint16_t)multicore_fifo_pop_blocking_inline());

            break;
        }
        case CMD_COPY_PAGE_FLASH_TO_SRAM:
        {
            rom_image[128] = 0xffff;
            memcpy(rom_image, (const void *)(XIP_BASE + arg * FLASH_PAGE_SIZE), FLASH_PAGE_SIZE);
            rom_image[128] = 0;
            break;
        }
        case CMD_COPY_PAGE_SRAM_TO_FLASH:
        {
            rom_image[128] = 0xffff;
            flash_range_program(arg * FLASH_PAGE_SIZE, (const uint8_t *)rom_image, FLASH_PAGE_SIZE);
            rom_image[128] = 0;
            break;
        }
        case CMD_ERASE_FLASH_SECTOR:
        {
            rom_image[128] = 0xffff;
            flash_range_erase(arg * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
            rom_image[128] = 0;
            break;
        }
    }
}

static void __not_in_flash_func(core1_main)()
{
    uint32_t magic_counter = 0;

    while (1)
    {
        uint32_t address = multicore_fifo_pop_blocking_inline();

        if (magic_counter == 0)
        {
            if (address == MAGIC_ADDR_0)
                magic_counter++;
        }
        else if (magic_counter == 1)
        {
            if (address == MAGIC_ADDR_1)
                magic_counter++;
            else
                magic_counter = 0;
        }
        else if (magic_counter == 2)
        {
            if (address == MAGIC_ADDR_2)
                magic_counter++;
            else
                magic_counter = 0;
        }
        else
        {
            handle_magic_read(address);
            magic_counter = 0;
        }
    }
}

static inline void multicore_fifo_push_non_blocking_inline(uint32_t data) {
    sio_hw->fifo_wr = data;
    __sev();
}

static void __not_in_flash_func(core0_main_rev6)()
{
    uint32_t triggered = 0;

    while (1)
    {
        uint64_t all_pins = gpio_get_all64();

        if ((all_pins & (1ULL << OE_PIN)) == 0)
        {
            uint32_t address = (all_pins >> 16) & ADDR_MASK;

            uint32_t value = (uint32_t)__builtin_bswap16(rom_image[address]);

            gpio_put_masked(DATA_MASK, value);
            gpio_set_dir_out_masked(DATA_MASK);

            if (!triggered)
            {
                multicore_fifo_push_non_blocking_inline(address);
                triggered = 1;
            }
        }
        else
        {
            gpio_set_dir_in_masked(DATA_MASK);
            triggered = 0;
        }
    }
}

static void __not_in_flash_func(core0_main_rev5)()
{
    uint32_t triggered = 0;

    while (1)
    {
        uint64_t all_pins = gpio_get_all64();

        if ((all_pins & (1ULL << OE_PIN)) == 0)
        {
            uint32_t address = (all_pins >> 16) & ((1 << 17) - 1);
            address |= (all_pins >> (BYTE_PIN - 17)) & (1 << 17);

            uint32_t value = (uint32_t)__builtin_bswap16(rom_image[address]);

            gpio_put_masked(DATA_MASK, value);
            gpio_set_dir_out_masked(DATA_MASK);

            if (!triggered)
            {
                multicore_fifo_push_non_blocking_inline(address);
                triggered = 1;
            }
        }
        else
        {
            gpio_set_dir_in_masked(DATA_MASK);
            triggered = 0;
        }
    }
}

void __not_in_flash_func(main)()
{
    set_sys_clock_khz(200000, false);

    gpio_set_dir_in_masked64((1ULL << 40) - 1ULL);

    for (uint i = 0; i < 40; i++)
        gpio_set_function(i, GPIO_FUNC_SIO);

    gpio_pull_down(BYTE_PIN);

    const uint32_t rom_slot = get_active_rom_slot();
    const uint32_t rom_slot_base = XIP_BASE + rom_slot * ROM_SLOT_SIZE;
    memcpy(rom_image, (const void *)rom_slot_base, sizeof(rom_image));

    bool rev6 = gpio_get(BYTE_PIN);
    gpio_disable_pulls(BYTE_PIN);

    multicore_launch_core1(core1_main);

    if (rev6)
        core0_main_rev6();
    else
        core0_main_rev5();
}
