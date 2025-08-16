#include "hardware/gpio.h"
#include "hardware/clocks.h"

#include <string.h>

#define CE_PIN 35
#define OE_PIN 36

#define DATA_MASK ((1 << 16) - 1)
#define ADDR_MASK ((1 << 18) - 1)

__attribute__((section(".rom_image")))
static uint16_t rom_image[256 * 1024];

static uint32_t magic_counter = 0;

static void __not_in_flash_func(read_access)(uint32_t address)
{
    if (magic_counter == 0)
    {
        if (address == (0x104 >> 1))
            magic_counter++;
    }
    else if (magic_counter == 1)
    {
        if (address == (0x894 >> 1))
            magic_counter++;
        else
            magic_counter = 0;
    }
    else if (magic_counter == 2)
    {
        if (address == (0x7f4fe >> 1))
            magic_counter++;
        else
            magic_counter = 0;
    }
    else
    {
        gpio_set_dir_in_masked(DATA_MASK);

        const uint32_t slot = address & 7;
        const uint32_t slot_address = 0x10000000 + slot * 0x80000;
        memcpy(rom_image, (const void *)slot_address, sizeof(rom_image));

        magic_counter = 0;
    }
}

static void __not_in_flash_func(rom_emulation)()
{
    uint32_t triggered = 0;

    while (true)
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
                read_access(address);
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

int __not_in_flash_func(main)()
{
    set_sys_clock_khz(200000, false);

    const uint32_t slot = 1;
    const uint32_t slot_address = 0x10000000 + slot * 0x80000;
    memcpy(rom_image, (const void *)slot_address, sizeof(rom_image));

    for (uint i = 0; i < 37; i++)
        gpio_init(i);

    rom_emulation();
}
