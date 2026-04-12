/*
 * RPROM firmware
 *
 * Copyright (C) 2025, 2026 Niklas Ekström
 * Copyright (C) 2026 Nico Bendlin - added PIO support
 */
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/busctrl.h"

#include "pico/flash.h"
#include "pico/multicore.h"

#include <string.h>

#include "protocol.h"

#define D0_PIN          0
#define A0_PIN          16
#define BYTE_PIN        34
#define CE_PIN          35
#define OE_PIN          36
#define RESET_PIN       37

#define DATA_PIO        pio0
#define DIRECTION_SM    0
#define DATA_SM         1

#define ADDRESS_PIO     pio1
#define ADDRESS_SM      1

#define CYCLE_SLOT_TIMEOUT_US   3000000 // 3 seconds

#define MAJOR_VERSION   1
#define MINOR_VERSION   1
#define PATCH_VERSION   1

#define ROM_SLOT_SIZE   (512 * 1024)

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

static void cycle_active_rom_slot()
{
    uint32_t prev_rom_slot = get_active_rom_slot();
    uint32_t rom_slot = prev_rom_slot;

    while (1)
    {
        rom_slot++;
        if (rom_slot == 8)
            rom_slot = 1;

        if (rom_slot == prev_rom_slot)
            return;

        // Check if this looks like a valid rom image.
        uint32_t rom_slot_base = XIP_BASE + rom_slot * ROM_SLOT_SIZE;
        if (*(uint8_t *)rom_slot_base == 0x11)
            break;
    }

    update_active_rom_slot_in_flash(rom_slot);

    uint32_t rom_slot_base = XIP_BASE + rom_slot * ROM_SLOT_SIZE;
    memcpy(rom_image, (const void *)rom_slot_base, sizeof(rom_image));
}

static void handle_magic_read(uint32_t address)
{
    uint32_t cmd = address >> 14;
    uint32_t arg = address & ((1 << 14) - 1);

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
            {
                while (!multicore_fifo_rvalid())
                    tight_loop_contents();

                rom_image[i] = __builtin_bswap16((uint16_t)(sio_hw->fifo_rd));
            }

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
        if (multicore_fifo_rvalid())
        {
            uint32_t address = sio_hw->fifo_rd;

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
        else if (!gpio_get(RESET_PIN))
        {
            uint32_t reset_started = time_us_32();

            while (!gpio_get(RESET_PIN))
            {
                int32_t elapsed = time_us_32() - reset_started;
                if (elapsed >= CYCLE_SLOT_TIMEOUT_US)
                {
                    gpio_put(RESET_PIN, false);
                    gpio_set_dir(RESET_PIN, true);

                    cycle_active_rom_slot();

                    gpio_set_dir(RESET_PIN, false);

                    reset_started = time_us_32();
                }
            }
        }
    }
}

static void __not_in_flash_func(core0_main)(bool a500_rev5)
{
    if (a500_rev5)
    {
        while (1)
        {
            uint32_t address = pio_sm_get_blocking(ADDRESS_PIO, ADDRESS_SM);
            address = ((address >> 1) & (1 << 17)) | (address & ((1 << 17) - 1));

            //uint32_t data = (uint32_t)__builtin_bswap16(rom_image[address]);
            uint32_t data;
            asm ("ldrh %0, [%1, %2, lsl #1]\n\t"
                "rev16 %0, %0"
                : "=r"(data) : "r"(rom_image), "r"(address));

            pio_sm_put(DATA_PIO, DATA_SM, data);

            sio_hw->fifo_wr = address;
        }
    }
    else
    {
        while (1)
        {
            uint32_t address = pio_sm_get_blocking(ADDRESS_PIO, ADDRESS_SM);

            //uint32_t data = (uint32_t)__builtin_bswap16(rom_image[address]);
            uint32_t data;
            asm ("ldrh %0, [%1, %2, lsl #1]\n\t"
                "rev16 %0, %0"
                : "=r"(data) : "r"(rom_image), "r"(address));

            pio_sm_put(DATA_PIO, DATA_SM, data);

            sio_hw->fifo_wr = address;
        }
    }
}

static void setup_data_pio_state_machines()
{
    DATA_PIO->gpiobase = 0;

    // Direction state machine.
    const uint direction_offset = 0;

    const uint16_t direction_instructions[] =
    {
                //     .wrap_target
        0xa063, //  0: mov    pindirs, null
        0x20c0, //  1: wait   1 irq, 0
        0xa06b, //  2: mov    pindirs, ~null
        0x20c1, //  3: wait   1 irq, 1
                //     .wrap
    };

    const uint direction_length = sizeof(direction_instructions) / sizeof(uint16_t);

    for (uint i = 0; i < direction_length; i++)
        DATA_PIO->instr_mem[direction_offset + i] = direction_instructions[i];

    pio_sm_config c_direction = pio_get_default_sm_config();
    sm_config_set_wrap(&c_direction, direction_offset, direction_offset + direction_length - 1);
    sm_config_set_out_pins(&c_direction, D0_PIN, 16);
    pio_sm_init(DATA_PIO, DIRECTION_SM, direction_offset, &c_direction);
    pio_sm_set_enabled(DATA_PIO, DIRECTION_SM, true);

    // Data state machine.
    const uint data_offset = direction_length;

    const uint16_t data_instructions[] =
    {
                //     .wrap_target
        0x6010, //  0: out    pins, 16
                //     .wrap
    };

    const uint data_length = sizeof(data_instructions) / sizeof(uint16_t);

    for (uint i = 0; i < data_length; i++)
        DATA_PIO->instr_mem[data_offset + i] = data_instructions[i];

    pio_sm_config c_data = pio_get_default_sm_config();
    sm_config_set_wrap(&c_data, data_offset, data_offset + data_length - 1);
    sm_config_set_out_pins(&c_data, D0_PIN, 16);
    sm_config_set_out_shift(&c_data, true, true, 16);
    pio_sm_init(DATA_PIO, DATA_SM, data_offset, &c_data);
    pio_sm_set_enabled(DATA_PIO, DATA_SM, true);
}

static void setup_address_pio_state_machines(bool a500_rev5)
{
    ADDRESS_PIO->gpiobase = 16;

    const uint address_offset = 0;
    const uint16_t in_pins = a500_rev5 ? 19 : 18;

    const uint16_t address_instructions[] =
    {
                                //     .wrap_target
        0x2000 + OE_PIN - 16,   //  0: wait   0 gpio, 36
        0x00c0,                 //  1: jmp    pin, 0
        0x4000 + in_pins,       //  2: in     pins, 18
        0xc008,                 //  3: irq    prev nowait 0
        0x2080 + OE_PIN - 16,   //  4: wait   1 gpio, 36
        0xc009,                 //  5: irq    prev nowait 1
                                //     .wrap
    };

    const uint address_length = sizeof(address_instructions) / sizeof(uint16_t);

    for (uint i = 0; i < address_length; i++)
        ADDRESS_PIO->instr_mem[address_offset + i] = address_instructions[i];

    pio_sm_config c_address = pio_get_default_sm_config();
    sm_config_set_wrap(&c_address, address_offset, address_offset + address_length - 1);
    sm_config_set_jmp_pin(&c_address, CE_PIN);
    sm_config_set_in_pins(&c_address, A0_PIN);
    sm_config_set_in_shift(&c_address, false, true, in_pins);
    pio_sm_init(ADDRESS_PIO, ADDRESS_SM, address_offset, &c_address);
    pio_sm_set_enabled(ADDRESS_PIO, ADDRESS_SM, true);
}

void __not_in_flash_func(main)()
{
    set_sys_clock_khz(200000, false);

    setup_data_pio_state_machines();

    gpio_set_dir_in_masked64((1ULL << 40) - 1ULL);

    for (uint i = 0; i < 40; i++)
        gpio_set_function(i, i < 16 ? GPIO_FUNC_PIO0 : GPIO_FUNC_SIO);

    gpio_pull_up(RESET_PIN);

    gpio_pull_down(BYTE_PIN);

    const uint32_t rom_slot = get_active_rom_slot();
    const uint32_t rom_slot_base = XIP_BASE + rom_slot * ROM_SLOT_SIZE;
    memcpy(rom_image, (const void *)rom_slot_base, sizeof(rom_image));

    bool a500_rev5 = gpio_get(BYTE_PIN) == 0;
    gpio_disable_pulls(BYTE_PIN);

    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC0_BITS;

    setup_address_pio_state_machines(a500_rev5);

    multicore_launch_core1(core1_main);

    core0_main(a500_rev5);
}
