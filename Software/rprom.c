#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CIAA_BASE   0xbfe001
#define ROM_BASE    0xf80000

#define CMD_UPDATE_ACTIVE_ROM_SLOT      0
#define CMD_WRITE_STATUS_TO_SRAM        1
#define CMD_RESTORE_PAGE_TO_SRAM        2
#define CMD_COPY_PAGE_AMIGA_TO_SRAM     3
#define CMD_COPY_PAGE_FLASH_TO_SRAM     4
#define CMD_COPY_PAGE_SRAM_TO_FLASH     5
#define CMD_ERASE_FLASH_SECTOR          6

static uint16_t sector_buffer[4096 / 2];

static void delay_us(int16_t us)
{
    volatile uint8_t *ciaa_pra = (volatile uint8_t *)CIAA_BASE;
    uint8_t tmp;

    for (int16_t i = us - 1; i >= 0; i--)
        tmp = *ciaa_pra;
}

static void send_command(uint32_t cmd, uint32_t arg)
{
    volatile uint16_t *rom = (volatile uint16_t *)ROM_BASE;
    uint16_t tmp;

    uint32_t offset = ((cmd & 0xf) << 14) | (arg & 0x3fff);

    tmp = rom[0x104 >> 1];
    tmp = rom[0x894 >> 1];
    tmp = rom[0x7f4fe >> 1];
    tmp = rom[offset];
}

static void copy_page_to_sram(uint16_t *src)
{
    volatile uint16_t *rom = (volatile uint16_t *)ROM_BASE;
    uint16_t tmp;

    for (int16_t i = 128 - 1; i >= 0; i--)
    {
        uint32_t offset = (uint32_t)*src++;
        tmp = rom[offset];
    }
}

static void copy_sram_page_to_buf(uint16_t *dst)
{
    volatile uint16_t *rom = (volatile uint16_t *)ROM_BASE;

    for (int16_t i = 128 - 1; i >= 0; i--)
        *dst++ = *rom++;
}

static void wait_operation_complete()
{
    volatile uint16_t *rom = (volatile uint16_t *)ROM_BASE;
    uint16_t tmp;

    do
    {
        delay_us(10);
        tmp = rom[128];
    }
    while (tmp);
}

// TODO: Decide on a final layout for this struct.
struct StatusV1
{
    uint8_t status_length;
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t patch_version;
    uint8_t flash_size_mb;
    uint8_t active_rom_slot;
};

static void status_command()
{
    Disable();
    send_command(CMD_WRITE_STATUS_TO_SRAM, 0);
    delay_us(10);
    copy_sram_page_to_buf(sector_buffer);
    send_command(CMD_RESTORE_PAGE_TO_SRAM, 0);
    Enable();

    struct StatusV1 *status = (struct StatusV1 *)sector_buffer;

    if (status->status_length != sizeof(struct StatusV1))
    {
        printf("Warning: Unexpected status length, read %u, expected %u\n",
            (uint16_t)status->status_length, (uint16_t)sizeof(struct StatusV1));
    }

    printf("RPROM status\n");
    printf("Firmware version: %u.%u.%u\n", (uint16_t)status->major_version,
        (uint16_t)status->minor_version, (uint16_t)status->patch_version);
    printf("Flash size:       %u MB\n", (uint16_t)status->flash_size_mb);
    printf("Active slot:      %u\n", (uint16_t)status->active_rom_slot);
}

static void switch_slot_command(uint32_t rom_slot)
{
    printf("Reset Amiga to boot to new kickstart\n");

    Disable();

    send_command(CMD_UPDATE_ACTIVE_ROM_SLOT, rom_slot);

    while (1)
        delay_us(1);
}

static void erase_slot_command(uint32_t rom_slot)
{
    printf("Erasing: [                ]\r"
           "Erasing: [");
    fflush(stdout);

    for (uint32_t sector_offset = 0; sector_offset < 128; sector_offset++)
    {
        Disable();
        uint32_t sector = rom_slot * 128 + sector_offset;
        send_command(CMD_ERASE_FLASH_SECTOR, sector);
        wait_operation_complete();
        Enable();

        if ((sector_offset & 7) == 7)
        {
            printf("#");
            fflush(stdout);
        }
    }

    Disable();
    send_command(CMD_RESTORE_PAGE_TO_SRAM, 0);
    Enable();

    printf( "]\n"
            "Erased slot %lu\n", rom_slot);
}

static void write_slot_command(uint32_t rom_slot, char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        printf("Error: Could not open file %s\n", filename);
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size != 512 * 1024)
    {
        printf("Error: File size must be exactly 512 kB\n");
        fclose(f);
        return;
    }

    printf("Writing: [                ]\r"
           "Writing: [");
    fflush(stdout);

    for (uint32_t sector_offset = 0; sector_offset < 128; sector_offset++)
    {
        size_t bytes_read = fread(sector_buffer, 1, sizeof(sector_buffer), f);

        if (bytes_read != sizeof(sector_buffer))
        {
            printf("\nError: Could not read enough data from file\n");
            fclose(f);
            return;
        }

        Disable();

        uint32_t sector = rom_slot * 128 + sector_offset;

        for (uint32_t page_offset = 0; page_offset < 16; page_offset++)
        {
            send_command(CMD_COPY_PAGE_AMIGA_TO_SRAM, 0);
            copy_page_to_sram(&sector_buffer[page_offset * 256 / 2]);
            send_command(CMD_COPY_PAGE_SRAM_TO_FLASH, sector * 16 + page_offset);
            wait_operation_complete();
        }

        Enable();

        if ((sector_offset & 7) == 7)
        {
            printf("#");
            fflush(stdout);
        }
    }

    Disable();
    send_command(CMD_RESTORE_PAGE_TO_SRAM, 0);
    Enable();

    printf( "]\n"
            "Wrote file %s to slot %lu\n", filename, rom_slot);
}

static void read_slot_command(uint32_t rom_slot, char *filename)
{
    FILE *f = fopen(filename, "wb");
    if (!f)
    {
        printf("Error: Could not open file %s\n", filename);
        return;
    }

    printf("Reading: [                ]\r"
           "Reading: [");
    fflush(stdout);

    for (uint32_t sector_offset = 0; sector_offset < 128; sector_offset++)
    {
        Disable();

        uint32_t sector = rom_slot * 128 + sector_offset;

        for (uint32_t page_offset = 0; page_offset < 16; page_offset++)
        {
            uint32_t page = sector * 16 + page_offset;

            send_command(CMD_COPY_PAGE_FLASH_TO_SRAM, page);
            wait_operation_complete();
            copy_sram_page_to_buf(&sector_buffer[page_offset * 256 / 2]);
        }

        Enable();

        size_t bytes_write = fwrite(sector_buffer, 1, sizeof(sector_buffer), f);

        if (bytes_write != sizeof(sector_buffer))
        {
            printf("\nError: Could not write enough data to file\n");
            fclose(f);
            return;
        }

        if ((sector_offset & 7) == 7)
        {
            printf("#");
            fflush(stdout);
        }
    }

    Disable();
    send_command(CMD_RESTORE_PAGE_TO_SRAM, 0);
    Enable();

    printf( "]\n"
            "Read slot %lu to file %s\n", rom_slot, filename);
}

enum Command
{
    ACMD_UNKNOWN,
    ACMD_STATUS,
    ACMD_SWITCH,
    ACMD_ERASE,
    ACMD_WRITE,
    ACMD_READ,
};

static enum Command parse_command(char *cmd)
{
    if (strcmp(cmd, "status") == 0)
        return ACMD_STATUS;
    else if (strcmp(cmd, "switch") == 0)
        return ACMD_SWITCH;
    else if (strcmp(cmd, "erase") == 0)
        return ACMD_ERASE;
    else if (strcmp(cmd, "write") == 0)
        return ACMD_WRITE;
    else if (strcmp(cmd, "read") == 0)
        return ACMD_READ;
    else
        return ACMD_UNKNOWN;
}

static void print_usage()
{
    printf( "Usage: RPROM command\n"
            "  where command is one of:\n"
            "    status\n"
            "    switch <slot>\n"
            "    erase <slot>\n"
            "    write <slot> <file>\n"
            "    read <slot> <file>\n");
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage();
        return 0;
    }

    char *cmd_str = argv[1];
    enum Command cmd = parse_command(cmd_str);
    if (cmd == ACMD_UNKNOWN)
    {
        printf("Error: Unknown command %s\n\n", cmd_str);
        print_usage();
        return 0;
    }

    uint32_t rom_slot = 0;

    if (cmd == ACMD_SWITCH || cmd == ACMD_ERASE || cmd == ACMD_WRITE || cmd == ACMD_READ)
    {
        if (argc < 3)
        {
            printf("Error: Missing slot argument\n\n");
            print_usage();
            return 0;
        }

        rom_slot = atoi(argv[2]);

        if (rom_slot < 1 || rom_slot > 7)
        {
            printf("Error: slot must be between 1 and 7\n");
            return 0;
        }
    }

    char *filename = NULL;

    if (cmd == ACMD_WRITE || cmd == ACMD_READ)
    {
        if (argc < 4)
        {
            printf("Error: Missing file argument\n\n");
            print_usage();
            return 0;
        }

        filename = argv[3];
    }

    if (cmd == ACMD_STATUS)
        status_command();
    else if (cmd == ACMD_SWITCH)
        switch_slot_command(rom_slot);
    else if (cmd == ACMD_ERASE)
        erase_slot_command(rom_slot);
    else if (cmd == ACMD_WRITE)
        write_slot_command(rom_slot, filename);
    else if (cmd == ACMD_READ)
        read_slot_command(rom_slot, filename);

    return 0;
}
