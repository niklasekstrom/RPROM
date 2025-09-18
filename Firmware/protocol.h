#ifndef RPROM_PROTOCOL_H
#define RPROM_PROTOCOL_H

#include <stdint.h>

#define MAGIC_ADDR_0    0x82
#define MAGIC_ADDR_1    0x44a
#define MAGIC_ADDR_2    0x3fa7f

#define CMD_UPDATE_ACTIVE_ROM_SLOT      0
#define CMD_WRITE_STATUS_TO_SRAM        1
#define CMD_RESTORE_PAGE_TO_SRAM        2
#define CMD_COPY_PAGE_AMIGA_TO_SRAM     3
#define CMD_COPY_PAGE_FLASH_TO_SRAM     4
#define CMD_COPY_PAGE_SRAM_TO_FLASH     5
#define CMD_ERASE_FLASH_SECTOR          6

struct StatusV1
{
    uint8_t magic[4];
    uint8_t status_length;
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t patch_version;
    uint8_t flash_size_mb;
    uint8_t active_rom_slot;
};

#define STATUS_V1_MAGIC "RPRM"

#endif // RPROM_PROTOCOL_H
