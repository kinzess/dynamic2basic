#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "bdev.h"
#include "gpt.h"

#define GPT_HEADER_SIGNATURE 0x5452415020494645ULL

int _read_header(int fd, gpt_header *header, uint64_t lba) {
    uint32_t crc, orig_crc32;

    if (bdev_read_lba(fd, lba, (uint8_t *)header, sizeof(gpt_header)) !=
        sizeof(gpt_header)) {
        printf("Error: failed to read lba\n");

        return -1;
    }

    if (le64toh(header->signature) != GPT_HEADER_SIGNATURE) {
        printf("Error: GPT header signature is wrong\n");
        return -1;
    }

    if (le32toh(header->header_size) > bdev_get_sector_size(fd)) {
        printf("Error: GPT header size is too large: %d\n",
               le32toh(header->header_size));
        return -1;
    }

    if (le32toh(header->header_size) < sizeof(gpt_header)) {
        printf("Error: GPT header size is too small: %d\n",
               le32toh(header->header_size));
        return -1;
    }

    orig_crc32 = le32toh(header->header_crc32);
    header->header_crc32 = 0;
    crc = crc32(0, (const void *)header, le32toh(header->header_size));
    if (orig_crc32 != crc) {
        printf("Error: GPT header CRC is wrong\n");
        return -1;
    }

    return 0;
}

int read_main_header(int fd, gpt_header *header) {
    uint64_t lba = GPT_PRIMARY_PARTITION_TABLE_LBA;
    return _read_header(fd, header, lba);
}

int read_second_header(int fd, gpt_header *header) {
    uint64_t lba;
    if (bdev_last_lba(fd, &lba)) {
        printf("Error: failed to get last lba\n");

        return -1;
    }

    return _read_header(fd, header, lba);
}

int read_gpt_header(int fd, gpt_header *header) {
    int is_alternate_lba = 0;
    uint64_t lba = GPT_PRIMARY_PARTITION_TABLE_LBA;

start:
    if (_read_header(fd, header, lba)) {
        if (is_alternate_lba == 0) {
            is_alternate_lba = 1;
            if (bdev_last_lba(fd, &lba)) {
                printf("Error: failed to get last lba\n");

                return -1;
            }

            goto start;
        }

        printf("Error: read gpt header failed\n");
        return -1;
    }

    return 0;
}

int read_gpt_entry(int fd, gpt_header *header, gpt_entry *entries,
                   uint64_t entry_size) {
    uint32_t crc;

    if (bdev_read_lba(fd, le64toh(header->partition_entry_lba),
                      (uint8_t *)entries, entry_size) != entry_size) {
        printf("Error: failed to read lba\n");
        return -1;
    }

    crc = crc32(0, (const Bytef *)entries, entry_size);
    if (crc != le32toh(header->partition_entry_array_crc32)) {
        printf("Error: GPT entries CRC is wrong\n");
        return -1;
    }

    return 0;
}

int write_gpt_header(int fd, gpt_header *header) {
    uint32_t crc = crc32(0, (const void *)header, le32toh(header->header_size));
    header->header_crc32 = crc;
    return bdev_write_lba(fd, header->current_lba, (uint8_t *)header,
                          sizeof(gpt_header));
}

int write_gpt_entry(int fd, gpt_header *header, gpt_entry *entries,
                    uint64_t entry_size) {
    return bdev_write_lba(fd, header->partition_entry_lba, (uint8_t *)entries,
                          entry_size);
}