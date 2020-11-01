#ifndef __GPT_H__
#define __GPT_H__

#include <stdint.h>
#include <uuid/uuid.h>

#define GPT_PRIMARY_PARTITION_TABLE_LBA 1

static const uuid_t PARTITION_BASIC_DATA_GUID = { 0xA2, 0xA0, 0xD0, 0xEB,
                                                  0xE5, 0xB9, 0x33, 0x44,
                                                  0x87, 0xC0, 0x68, 0xB6,
                                                  0xB7, 0x26, 0x99, 0xC7 };

typedef struct _gpt_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved1;
    uint64_t current_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uuid_t disk_guid;
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t sizeof_partition_entry;
    uint32_t partition_entry_array_crc32;
} __attribute__((__packed__)) gpt_header;

typedef struct _gpt_entry {
    uuid_t type;
    uuid_t guid;
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t flags;
    char name[72];
} __attribute__((__packed__)) gpt_entry;

int read_gpt_header(int fd, gpt_header *header);
int read_main_header(int fd, gpt_header *header);
int read_second_header(int fd, gpt_header *header);
int read_gpt_entry(int fd, gpt_header *header, gpt_entry *entries,
                   uint64_t entry_size);

int write_gpt_header(int fd, gpt_header *header);
int write_gpt_entry(int fd, gpt_header *header, gpt_entry *entries,
                    uint64_t entry_size);

#endif
