#ifndef __LDM_H__
#define __LDM_H__

#include <stdint.h>
#include <uuid/uuid.h>

#include "gpt.h"
#include "list.h"
#include "mbr.h"

#define MBR_PRIVHEAD_SECTOR 6

static const uuid_t PARTITION_LDM_METADATA_GUID = { 0xAA, 0xC8, 0x08, 0x58,
                                                    0x8F, 0x7E, 0xE0, 0x42,
                                                    0x85, 0xD2, 0xE1, 0xE9,
                                                    0x04, 0x34, 0xCF, 0xB3 };

static const uuid_t PARTITION_LDM_DATA_GUID = { 0xA0, 0x60, 0x9B, 0xAF,
                                                0x31, 0x14, 0x62, 0x4F,
                                                0xBC, 0x68, 0x33, 0x11,
                                                0x71, 0x4A, 0x69, 0xAD };

typedef struct _partition_data {
    struct list_head list;

    uint64_t start;
    uint64_t offset;
    uint64_t size;
    uint8_t part_type;
} partition_data;

typedef struct _vblk_volume {
    struct list_head list;

    uint32_t id;
    char *name;
    uint8_t type;
    uint8_t flags;
    uint32_t num_of_comps;
    uint64_t size;
    uint8_t part_type;
    uuid_t guid;
    char *hint;
} __attribute__((__packed__)) vblk_volume;

typedef struct _vblk_component {
    struct list_head list;

    uint32_t id;
    char *name;
    uint8_t type;
    uint8_t flags;
    uint32_t num_of_parts;
    uint32_t volume_id;
    uint64_t chunk_size;
    uint32_t columns;
} __attribute__((__packed__)) vblk_component;

typedef struct _vblk_partition {
    struct list_head list;

    uint32_t id;
    char *name;
    uint64_t start;
    uint64_t volume_offset;
    uint64_t size;
    uint32_t component_id;
    uint32_t disk_id;
    uint32_t index;
} __attribute__((__packed__)) vblk_partition;

typedef struct _vblk_disk {
    struct list_head list;

    uint32_t id;
    char *name;
    uuid_t guid;
} __attribute__((__packed__)) vblk_disk;

typedef struct _vblk_disk_group {
    struct list_head list;

    uint32_t id;
    char *name;
} __attribute__((__packed__)) vblk_disk_group;

typedef struct _vblk_extended {
    struct list_head list;

    uint32_t group_number;
    uint16_t num_records;
    uint16_t num_records_found;
    uint8_t *data;
} __attribute__((__packed__)) vblk_extended;

typedef struct _vblk_record {
    uint16_t status;
    uint8_t flags;
    uint8_t type;
    uint32_t size;
} __attribute__((__packed__)) vblk_record;

typedef struct _vblk_head {
    char magic[4]; // "VBLK"

    uint32_t sequence_number;

    uint32_t group_number;
    uint16_t record_number;
    uint16_t num_records;
} __attribute__((__packed__)) vblk_head;

typedef struct _vmdb {
    char magic[4]; // "VMDB"

    uint32_t vblk_last;
    uint32_t vblk_size;
    uint32_t vblk_first_offset;

    uint16_t update_status;

    uint16_t version_major;
    uint16_t version_minor;

    char disk_group_name[31];
    char disk_group_guid[64];

    uint64_t committed_seq;
    uint64_t pending_seq;
    uint32_t num_committed_vblks_vol;
    uint32_t num_committed_vblks_comp;
    uint32_t num_committed_vblks_part;
    uint32_t num_committed_vblks_disk;
    char padding1[12];
    uint32_t num_pending_vblks_vol;
    uint32_t num_pending_vblks_comp;
    uint32_t num_pending_vblks_part;
    uint32_t num_pending_vblks_disk;
    char padding2[12];

    uint64_t last_accessed;
} __attribute__((__packed__)) vmdb;

typedef struct _tocblock_bitmap {
    char name[8];
    uint16_t flags1;
    uint64_t start;
    uint64_t size;
    uint64_t flags2;
} __attribute__((__packed__)) tocblock_bitmap;

typedef struct _tocblock {
    char magic[8]; // "TOCBLOCK"

    uint32_t seq1;
    char padding1[4];
    uint32_t seq2;
    char padding2[16];

    tocblock_bitmap bitmap[2];
} __attribute__((__packed__)) tocblock;

typedef struct _privhead {
    char magic[8]; // "PRIVHEAD"

    uint32_t unknown_sequence;
    uint16_t version_major;
    uint16_t version_minor;

    uint64_t unknown_timestamp;
    uint64_t unknown_number;
    uint64_t unknown_size1;
    uint64_t unknown_size2;

    char disk_guid[64];
    char host_guid[64];
    char disk_group_guid[64];
    char disk_group_name[32];

    uint16_t unknown1;
    char padding1[9];

    uint64_t logical_disk_start;
    uint64_t logical_disk_size;
    uint64_t ldm_config_start;
    uint64_t ldm_config_size;
    uint64_t n_tocs;
    uint64_t toc_size;
    uint32_t n_configs;
    uint32_t n_logs;
    uint64_t config_size;
    uint64_t log_size;

    uint32_t disk_signature;
    /* Values below aren't set in my data */
    char disk_set_guid[16];
    char disk_set_guid_dup[16];

} __attribute__((__packed__)) privhead;

int read_mbr_ldm(int fd, struct list_head *new_entries);
int read_gpt_ldm(int fd, gpt_header *header, gpt_entry **entries,
                 struct list_head *new_entries);

#endif /* __LDM_H__ */