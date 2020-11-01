#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include "gpt.h"
#include "ldm.h"
#include "list.h"
#include "mbr.h"

int saveGPT(int fd, gpt_entry *entries, struct list_head *new_entries) {
    char input[128];
    int i;
    int isOK = 1;
    gpt_header main_header, second_header;
    gpt_entry zero_entry = { 0 };
    uint64_t entries_size;
    uint32_t crc;

    if (read_main_header(fd, &main_header) != 0 ||
        read_second_header(fd, &second_header) != 0) {
        printf("Error: failed to read main header or secondary header\n");
        return -1;
    }

    if ((main_header.partition_entry_array_crc32 !=
         second_header.partition_entry_array_crc32) ||
        (main_header.alternate_lba != second_header.current_lba) ||
        (main_header.current_lba != second_header.alternate_lba) ||
        (main_header.header_crc32 != 0) || (second_header.header_crc32 != 0)) {
        printf("Error: gpt header not match");
        return -1;
    }

    // clear ldm entry
    for (i = 0; i < le32toh(main_header.num_partition_entries); i++) {
        if (!uuid_compare(entries[i].type, PARTITION_LDM_DATA_GUID) ||
            !uuid_compare(entries[i].type, PARTITION_LDM_METADATA_GUID)) {
            memset(&entries[i], 0, sizeof(gpt_entry));
        }
    }

    // update entry
    struct list_head *pos;
    i = 0;
    list_for_each(pos, new_entries) {
        partition_data *part = list_entry(pos, partition_data, list);
        printf("partion %d start=%lu size=%lu part type=%d\n", i++, part->start,
               part->size, part->part_type);
    }

    printf("Warning, are you sure to save the new partition table shown above? "
           "(yes or no)\n");
    scanf("%s", input);
    if (strcmp(input, "yes")) {
        printf("exit.\n");
        exit(0);
    }

    i = 0;
    list_for_each(pos, new_entries) {
        partition_data *part = list_entry(pos, partition_data, list);

        for (i = 0; i < le32toh(main_header.num_partition_entries); i++) {
            if (!memcmp(&entries[i], &zero_entry, sizeof(gpt_entry))) {
                gpt_entry *entry = &entries[i];
                uuid_copy(entry->type, PARTITION_BASIC_DATA_GUID);
                uuid_generate_random(entry->guid);
                entry->first_lba = part->start;
                entry->last_lba = part->start + part->size - 1;
                entry->flags = 0;
                int a = sizeof(entry->name);
                memset(entry->name, 0, sizeof(entry->name));
                break;
            }
        }
    }

    // generate new crc
    entries_size = le32toh(main_header.num_partition_entries) *
                   le32toh(main_header.sizeof_partition_entry);
    crc = crc32(0, (const Bytef *)entries, entries_size);
    second_header.partition_entry_array_crc32 = crc;
    main_header.partition_entry_array_crc32 = crc;

    // save
    if (write_gpt_entry(fd, &second_header, entries, entries_size) !=
        entries_size) {
        printf("Error: failed to save alternate entries\n");
        return -1;
    }

    if (write_gpt_header(fd, &second_header) != sizeof(gpt_header)) {
        printf("Error: failed to save alternate header\n");
        return -1;
    }

    if (write_gpt_entry(fd, &main_header, entries, entries_size) !=
        entries_size) {
        printf("Error: failed to save main entries\n");
        return -1;
    }

    if (write_gpt_header(fd, &main_header) != sizeof(gpt_header)) {
        printf("Error: failed to save main header\n");
        return -1;
    }

    return 0;
}

int saveMBR(int fd, legacy_mbr *mbr, struct list_head *new_entries) {
    char input[128];
    int i = 0;
    struct list_head *pos;

    list_for_each(pos, new_entries) {
        partition_data *part = list_entry(pos, partition_data, list);
        printf("partion %d start=%lu end=%lu size=%lu part type=%d\n", i++,
               part->start, part->start + part->size - 1, part->size,
               part->part_type);
    }

    if (i > 4) {
        printf("Error: found %d partitions, currently does not support "
               "extended partitions.\n",
               i);
        return -1;
    }

    printf("Warning, are you sure to save the new partition table shown above? "
           "(yes or no)\n");
    scanf("%s", input);
    if (strcmp(input, "yes")) {
        printf("exit.\n");
        exit(0);
    }

    i = 0;
    list_for_each(pos, new_entries) {
        partition_data *part = list_entry(pos, partition_data, list);

        mbr_partition *mbr_part = &(mbr->partition[i++]);
        mbr_part->boot_indicator = 0x0;
        calcCHS(part->start, &mbr_part->start_track, &mbr_part->start_head,
                &mbr_part->start_sector);
        mbr_part->os_type = part->part_type;
        calcCHS(part->start + part->size, &mbr_part->end_track,
                &mbr_part->end_head, &mbr_part->end_sector);
        mbr_part->starting_lba = part->start;
        mbr_part->size_in_lba = part->size;
    }

    if (write_mbr(fd, mbr) != sizeof(legacy_mbr)) {
        printf("Error: failed to save mbr.\n");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    char input[128];
    if (argc != 2) {
        printf("Usage: b2c /dev/device\n");
        return -1;
    }

    printf("Warning, please use other tools to save the partition table "
           "first!!!\n");
    printf("continue? (yes or no)\n");
    scanf("%s", input);
    if (strcmp(input, "yes")) {
        printf("exit.\n");
        return 0;
    }

    char *dev = argv[1];

    extern int errno;
    legacy_mbr mbr;

    struct list_head new_entries = LIST_HEAD_INIT(new_entries);
    struct list_head *pos, *next;

    // open device
    const int fd = open(dev, O_RDWR);
    if (fd == -1) {
        printf("Error: failed to open %s, errno is %d\n", dev, errno);
        return -1;
    }

    // read mbr first
    if (read_mbr(fd, &mbr) != MBR_ERROR_OK) {
        printf("Error: failed to read mbr\n");
        return -1;
    }

    switch (mbr.partition[0].os_type) {
    case MBR_PART_EFI_PROTECTIVE: {
        printf("Info: Device %s use GPT\n", dev);
        gpt_header header;
        gpt_entry *entries = NULL;

        if (read_gpt_ldm(fd, &header, &entries, &new_entries)) {
            printf("Error: read gpt ldm info failed.\n");
        } else {
            if (saveGPT(fd, entries, &new_entries)) {
                printf("Error: save gpt failed.\n");
            }
        }

        if (entries)
            free(entries);
    } break;
    case MBR_PART_WINDOWS_LDM: {
        printf("Info: Device %s use MBR\n", dev);

        if (read_mbr_ldm(fd, &new_entries)) {
            printf("Error: read mbr ldm info failed.\n");
        } else {
            if (saveMBR(fd, &mbr, &new_entries)) {
                printf("Error: save mbr failed.\n");
            }
        }
    } break;
    default:
        printf("Info: Device %s is not a valid LDM disk\n", dev);
        return -1;
    }

    list_for_each_safe(pos, next, &new_entries) {
        partition_data *part = list_entry(pos, partition_data, list);
        free(part);
    }

    close(fd);

    return 0;
}