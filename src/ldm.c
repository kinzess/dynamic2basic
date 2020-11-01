#include <endian.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

#include "bdev.h"
#include "debug.h"
#include "ldm.h"
#include "mbr.h"

enum {
    VBLK_BLACK = 0,
    VBLK_VOLUME,
    VBLK_COMPONENT,
    VBLK_PARTITION,
    VBLK_DISK,
    VBLK_DISK_GROUP,
};

enum {
    VOLUME_TYPE_GEN = 0x3,
    VOLUME_TYPE_RAID5 = 0x4,
};

enum {
    VOLUME_FLAG_ID1 = 0x08,
    VOLUME_FLAG_ID2 = 0x20,
    VOLUME_FLAG_SIZE = 0x80,
    VOLUME_FLAG_DRIVE_HINT = 0x02,
};

enum {
    COMPONENT_TYPE_STRIPED = 0x1,
    COMPONENT_TYPE_SPANNED = 0x2,
    COMPONENT_TYPE_RAID = 0x3
};

enum {
    COMPONENT_FLAG_ENABLE = 0x10,
};

enum {
    PARTITION_FLAG_INDEX = 0x08,
};

static uuid_t cur_dev_guid;
static struct list_head ext_vblk_list = LIST_HEAD_INIT(ext_vblk_list);

static struct list_head volume_list = LIST_HEAD_INIT(volume_list);
static struct list_head component_list = LIST_HEAD_INIT(component_list);
static struct list_head partition_list = LIST_HEAD_INIT(partition_list);
static struct list_head disk_list = LIST_HEAD_INIT(disk_list);
static struct list_head disk_group_list = LIST_HEAD_INIT(disk_group_list);

static int parse_var_uint64_t(const uint8_t **const var, uint64_t *ret) {
    uint8_t len = **var;
    (*var)++;

    if (len > sizeof(uint64_t)) {
        printf("ldm: found %d bytes integer\n", len);
        return -1;
    }

    *ret = 0;
    for (; len > 0; len--) {
        *ret <<= 8;
        *ret += **var;
        (*var)++;
    }

    return 0;
}

static int parse_var_uint32_t(const uint8_t **const var, uint32_t *ret) {
    uint8_t len = **var;
    (*var)++;

    if (len > sizeof(uint32_t)) {
        printf("ldm: found %d bytes integer\n", len);
        return -1;
    }

    *ret = 0;
    for (; len > 0; len--) {
        *ret <<= 8;
        *ret += **var;
        (*var)++;
    }

    return 0;
}

static int parse_var_string(const uint8_t **const var, char **ret) {
    uint8_t len = **var;
    (*var)++;

    *ret = calloc(len + 1, sizeof(uint8_t));
    if (!*ret) {
        printf("ldm: failed to invoke calloc\n");
        return -1;
    }

    memcpy(*ret, *var, len);
    (*var) += len;

    return 0;
}

static inline void parse_var_skip(const uint8_t **const var) {
    uint8_t len = **var;
    (*var)++;
    (*var) += len;
}

static int parse_vblk_volume(const uint8_t *vblk_data, const uint8_t revision,
                             uint8_t field_flags) {
    uint32_t id;
    char *name;
    uint8_t type;
    uint8_t flags;
    uint32_t numOfChildren;
    uint64_t size, size1 = 0;
    uint8_t partitionType;
    uuid_t guid;
    char *id1 = NULL, *id2 = NULL, *driveHint = NULL;

    if (revision != 5) {
        printf("ldm: not support volume revision: %hhu\n", revision);
        return -1;
    }

    /* volume id */
    if (parse_var_uint32_t(&vblk_data, &id))
        return -1;

    /* volume name */
    if (parse_var_string(&vblk_data, &name))
        return -1;

    /* volume type 1 */
    parse_var_skip(&vblk_data);

    /* unknown */
    parse_var_skip(&vblk_data);

    /* volume state */
    vblk_data += 14;

    /* volume type 2 */
    type = *(uint8_t *)vblk_data;
    vblk_data++;
    if (type != VOLUME_TYPE_GEN && type != VOLUME_TYPE_RAID5) {
        printf("ldm: not support volume type: %d\n", type);
        free(name);
        return -1;
    }

    /* unknown */
    vblk_data++;

    /* volume number */
    vblk_data++;

    /* zeros */
    vblk_data += 3;

    /* volume flags */
    flags = *(uint8_t *)vblk_data;
    vblk_data++;

    /* number of children */
    if (parse_var_uint32_t(&vblk_data, &numOfChildren))
        return -1;

    /* log commit id */
    vblk_data += 8;

    /* id? or 0x00 */
    vblk_data += 8;

    /* size */
    if (parse_var_uint64_t(&vblk_data, &size))
        return -1;

    /* zeros */
    vblk_data += 4;

    /* partition type */
    partitionType = *(uint8_t *)vblk_data;
    vblk_data++;

    /* guid */
    uuid_copy(guid, vblk_data);
    vblk_data += sizeof(uuid_t);

    if (field_flags & VOLUME_FLAG_ID1) {
        if (parse_var_string(&vblk_data, &id1))
            return -1;
    } else if (field_flags & VOLUME_FLAG_ID2) {
        if (parse_var_string(&vblk_data, &id2))
            return -1;
    } else if (field_flags & VOLUME_FLAG_SIZE) {
        if (parse_var_uint64_t(&vblk_data, &size1))
            return -1;
    } else if (field_flags & VOLUME_FLAG_DRIVE_HINT) {
        if (parse_var_string(&vblk_data, &driveHint))
            return -1;
    }

    D("volume: %s\n"
      "  ID: %d \n"
      "  Type: %d\n"
      "  Flags: %d\n"
      "  Children: %d\n"
      "  Size: %lu \n"
      "  Partition Type: %d \n"
      "  ID1: %s\n"
      "  ID2: %s\n"
      "  Size1: %lu \n"
      "  Hint: %s\n\n",
      name, id, type, flags, numOfChildren, size, partitionType, id1, id2,
      size1, driveHint);

    vblk_volume *volume = malloc(sizeof(vblk_volume));
    volume->id = id;
    volume->name = name;
    volume->type = type;
    volume->flags = flags;
    volume->num_of_comps = numOfChildren;
    volume->size = size;
    volume->part_type = partitionType;
    uuid_copy(volume->guid, guid);
    volume->hint = driveHint;

    list_add(&(volume->list), &volume_list);
    return 0;
}

static int parse_vblk_component(const uint8_t *vblk_data,
                                const uint8_t revision, uint8_t field_flags) {
    uint32_t id;
    char *name;
    uint8_t type;
    uint32_t numOfChildren;
    uint32_t parentID;
    uint64_t chunkSize = 0;
    uint32_t columns = 0;

    if (revision != 3) {
        printf("ldm: not support component revision: %hhu\n", revision);
        return -1;
    }

    /* component id */
    if (parse_var_uint32_t(&vblk_data, &id))
        return -1;

    /* component name */
    if (parse_var_string(&vblk_data, &name))
        return -1;

    /* component state */
    parse_var_skip(&vblk_data);

    /* component type */
    type = *((uint8_t *)vblk_data);
    vblk_data++;
    if (type != COMPONENT_TYPE_SPANNED) {
        printf("ldm: not support component type: %d\n", type);
        return -1;
    }

    /* zeros */
    vblk_data += 4;

    /* number of children */
    if (parse_var_uint32_t(&vblk_data, &numOfChildren))
        return -1;

    /* commit id */
    vblk_data += 8;

    /* zeros1 */
    vblk_data += 8;

    /* parent id */
    if (parse_var_uint32_t(&vblk_data, &parentID))
        return -1;

    /* zeros2 */
    vblk_data++;

    if (field_flags & COMPONENT_FLAG_ENABLE) {
        if (parse_var_uint64_t(&vblk_data, &chunkSize))
            return -1;
        if (parse_var_uint32_t(&vblk_data, &columns))
            return -1;
    }

    D("Component:\n"
      "  ID: %d \n"
      "  Parent ID: %d \n"
      "  Type: %d\n"
      "  Parts: %d\n"
      "  Chunk Size: %lu\n"
      "  Columns: %d\n\n",
      id, parentID, type, numOfChildren, chunkSize, columns);

    vblk_component *component = malloc(sizeof(vblk_component));
    component->id = id;
    component->name = name;
    component->type = type;
    component->num_of_parts = numOfChildren;
    component->volume_id = parentID;
    component->chunk_size = chunkSize;
    component->columns = columns;

    list_add(&(component->list), &component_list);

    return 0;
}

static int parse_vblk_partition(const uint8_t *vblk_data,
                                const uint8_t revision, uint8_t field_flags) {
    uint32_t id;
    char *name;
    uint64_t start;
    uint64_t offset;
    uint64_t size;
    uint32_t parentID, diskID;
    uint32_t index = 0;

    if (revision != 3) {
        printf("ldm: not support partition revision: %hhu\n", revision);
        return -1;
    }

    /* partition id */
    if (parse_var_uint32_t(&vblk_data, &id))
        return -1;

    /* partition name */
    if (parse_var_string(&vblk_data, &name))
        return -1;

    /* zeros */
    vblk_data += 4;

    /* commit id */
    vblk_data += 8;

    start = be64toh(*(uint64_t *)vblk_data);
    vblk_data += 8;
    offset = be64toh(*(uint64_t *)vblk_data);
    vblk_data += 8;

    if (parse_var_uint64_t(&vblk_data, &size))
        return -1;

    if (parse_var_uint32_t(&vblk_data, &parentID))
        return -1;

    if (parse_var_uint32_t(&vblk_data, &diskID))
        return -1;

    if (field_flags & PARTITION_FLAG_INDEX) {
        if (parse_var_uint32_t(&vblk_data, &index))
            return -1;
    }

    D("Partition: %s\n"
      "  ID: %d\n"
      "  Parent ID: %d\n"
      "  Disk ID: %d\n"
      "  Index: %d\n"
      "  Start: %lu\n"
      "  Vol Offset: %lu\n"
      "  Size: %lu\n\n",
      name, id, parentID, diskID, index, start, offset, size);

    vblk_partition *part = malloc(sizeof(vblk_partition));
    part->id = id;
    part->name = name;
    part->start = start;
    part->volume_offset = offset;
    part->size = size;
    part->component_id = parentID;
    part->disk_id = diskID;
    part->index = index;

    list_add(&(part->list), &partition_list);

    return 0;
}

static int parse_vblk_disk(const uint8_t *vblk_data, const uint8_t revision,
                           uint8_t field_flags) {
    uint32_t id;
    char *name;
    uuid_t guid;

    /* disk id */
    if (parse_var_uint32_t(&vblk_data, &id))
        return -1;

    /* disk name */
    if (parse_var_string(&vblk_data, &name))
        return -1;

    if (revision == 3) {
        char *idStr;
        if (parse_var_string(&vblk_data, &idStr))
            return -1;
        if (uuid_parse(idStr, (unsigned char *)&guid) == -1) {
            printf("ldm: disk %d has invalid guid: %s\n", id, idStr);
            return -1;
        }
        free(idStr);
    } else if (revision == 4) {
        uuid_copy(guid, vblk_data);
        vblk_data += sizeof(uuid_t);
    } else {
        printf("ldm: not support disk revision: %hhu\n", revision);
        return -1;
    }

    char out[64] = { 0 };
    uuid_unparse_lower((unsigned char *)&guid, out);
    D("Disk: %s\n"
      "  ID: %u\n"
      "  GUID: %s\n\n",
      name, id, out);

    vblk_disk *disk = malloc(sizeof(vblk_disk));
    disk->id = id;
    disk->name = name;
    uuid_copy(disk->guid, guid);

    list_add(&(disk->list), &disk_list);

    return 0;
}

static int parse_vblk_disk_group(const uint8_t *vblk_data,
                                 const uint8_t revision, uint8_t field_flags) {
    uint32_t id;
    char *name;

    if (revision != 3 && revision != 4) {
        printf("ldm: not support disk group revision: %hhu\n", revision);
        return -1;
    }

    /* partition id */
    if (parse_var_uint32_t(&vblk_data, &id))
        return -1;

    /* partition name */
    if (parse_var_string(&vblk_data, &name))
        return -1;

    D("Disk Group: %s\n"
      "  ID: %u\n\n",
      name, id);

    vblk_disk_group *dg = malloc(sizeof(vblk_disk_group));
    dg->id = id;
    dg->name = name;

    list_add(&(dg->list), &disk_group_list);

    return 0;
}

static int parse_vblk(const void *vblk_data) {
    const vblk_record *const rec = vblk_data;
    uint8_t type = rec->type & 0x0F;
    uint8_t revision = (rec->type & 0xF0) >> 4;

    vblk_data += sizeof(vblk_record);

    switch (type) {
    case VBLK_BLACK:
        break;
    case VBLK_VOLUME:
        if (parse_vblk_volume(vblk_data, revision, rec->flags))
            return -1;
        break;
    case VBLK_COMPONENT:
        if (parse_vblk_component(vblk_data, revision, rec->flags))
            return -1;
        break;
    case VBLK_PARTITION:
        if (parse_vblk_partition(vblk_data, revision, rec->flags))
            return -1;
        break;
    case VBLK_DISK:
        if (parse_vblk_disk(vblk_data, revision, rec->flags))
            return -1;
        break;
    case VBLK_DISK_GROUP:
        if (parse_vblk_disk_group(vblk_data, revision, rec->flags))
            return -1;
        break;
    default:
        return -1;
        break;
    }

    return 0;
}

static int read_vblks(int fd, const vmdb *const db) {
    const void *vblk = (void *)db + be32toh(db->vblk_first_offset);
    const uint32_t vblk_size = be32toh(db->vblk_size);
    const uint32_t vblk_data_size = vblk_size - (sizeof(vblk_head));

    struct list_head *pos;
    vblk_extended *ext_vblk;

    for (;;) {
        const vblk_head *const head = vblk;
        if (memcmp(head->magic, "VBLK", 4) != 0)
            break;

        if (be16toh(head->num_records) > 0 &&
            be16toh(head->record_number) >= be16toh(head->num_records)) {
            printf("ldm: vblk record not valid\n");
            return -1;
        }

        vblk += sizeof(vblk_head);

        if (be16toh(head->num_records) > 1) {
            int found = 0;

            printf("head has %d records\n", be16toh(head->num_records));

            list_for_each(pos, &ext_vblk_list) {
                ext_vblk = list_entry(pos, vblk_extended, list);
                if (ext_vblk->group_number == head->group_number) {
                    ext_vblk->num_records_found++;
                    memcpy(ext_vblk->data[be16toh(head->record_number) *
                                           vblk_data_size],
                           vblk, vblk_data_size);
                    found = 1;
                    break;
                }
            }

            if (!found) {
                vblk_extended *new_ext_vblk = malloc(sizeof(vblk_extended));
                new_ext_vblk->group_number = head->group_number;
                new_ext_vblk->num_records = be16toh(head->num_records);
                new_ext_vblk->num_records_found = 1;
                new_ext_vblk->data =
                    malloc(be16toh(head->num_records * vblk_data_size));
                memcpy(new_ext_vblk->data[be16toh(head->record_number) *
                                          vblk_data_size],
                       vblk, vblk_data_size);

                list_add(&(new_ext_vblk->list), &ext_vblk_list);
            }
        } else {
            parse_vblk(vblk);
        }

        vblk += vblk_data_size;
    }

    return 0;
}

static privhead *alloc_read_privhead(int fd, uint64_t lba) {
    privhead *header;

    header = malloc(sizeof(privhead));
    if (!header) {
        printf("ldm: failed to malloc\n");
        return header;
    }

    if (bdev_read_lba(fd, lba, (uint8_t *)header, sizeof(*header)) !=
        sizeof(*header)) {
        printf("ldm: failed to read privheader\n");
        free(header);
        return NULL;
    }

    if (memcmp(header->magic, "PRIVHEAD", 8)) {
        printf("ldm: not found PRIVHEAD\n");
        free(header);
        return NULL;
    }

    D("disk guid: %s\nhost guid: %s\ndisk group guid:%s\ndisk group "
      "name: "
      "%s\n\n",
      header->disk_guid, header->host_guid, header->disk_group_guid,
      header->disk_group_name);

    return header;
}

static uint8_t *alloc_read_config(int fd, privhead *header) {
    uint8_t *config = NULL;
    uint64_t config_start = be64toh(header->ldm_config_start);
    uint64_t config_size =
        be64toh(header->ldm_config_size) * bdev_get_sector_size(fd);

    config = malloc(config_size);
    if (!config) {
        printf("ldm: failed to malloc\n");
        return NULL;
    }

    if (bdev_read_lba(fd, config_start, config, config_size) != config_size) {
        printf("ldm: failed to read config\n");
        free(config);
        return NULL;
    }

    return config;
}

static int read_ldm(int fd, uint64_t lba, privhead **head) {
    int i;
    uint8_t *config;
    tocblock *toc_block;
    tocblock_bitmap *bitmap;
    vmdb *db = NULL;

    *head = alloc_read_privhead(fd, lba);
    if (!head) {
        return -1;
    }

    if (uuid_parse((*head)->disk_guid, (unsigned char *)&cur_dev_guid) == -1) {
        printf("ldm: disk has invalid guid: %s\n", (*head)->disk_guid);
        return -1;
    }

    config = alloc_read_config(fd, *head);
    if (!config) {
        free(head);
        return -1;
    }

    toc_block = config + bdev_get_sector_size(fd) * 2;
    if (memcmp(toc_block->magic, "TOCBLOCK", 8) != 0) {
        printf("ldm: not found TOCBLOCK\n");
        free(config);
        free(head);
        return -1;
    }

    for (i = 0; i < 2; i++) {
        bitmap = &toc_block->bitmap[i];
        if (!memcmp(bitmap->name, "config", 6)) {
            db = config + be64toh(bitmap->start) * bdev_get_sector_size(fd);
            break;
        }
    }

    if (!db || !memcpy(db->magic, "VMDB", 4)) {
        printf("ldm: not found VMDB\n");
        free(config);
        free(head);
        return -1;
    }

    read_vblks(fd, db);

    free(config);

    return 0;
}

int parse_ldm(uint64_t start, struct list_head *new_entries) {
    struct list_head *pos;

    vblk_disk *disk;
    uint32_t disk_id = 0;

    vblk_partition *partition;

    list_for_each(pos, &disk_list) {
        disk = list_entry(pos, vblk_disk, list);
        if (!uuid_compare(disk->guid, cur_dev_guid)) {
            disk_id = disk->id;
            break;
        }
    }

    if (disk_id < 0) {
        printf("disk guid not match\n");
        return -1;
    }

    list_for_each(pos, &partition_list) {
        struct list_head *com_pos, *vol_pos;
        vblk_component *com = NULL;
        vblk_volume *vol = NULL;

        partition = list_entry(pos, vblk_partition, list);

        if (partition->disk_id != disk_id)
            continue;

        list_for_each(com_pos, &component_list) {
            com = list_entry(com_pos, vblk_component, list);
            if (com->id == partition->component_id) {
                break;
            }

            com = NULL;
        }

        if (!com) {
            printf("not found compoment, id: %d", partition->component_id);
            return -1;
        }

        list_for_each(vol_pos, &volume_list) {
            vol = list_entry(vol_pos, vblk_volume, list);
            if (vol->id == com->volume_id) {
                break;
            }

            vol = NULL;
        }

        if (!vol) {
            printf("not found volume, id: %d", com->volume_id);
            return -1;
        }

        if (vol && com && partition) {
            D("Data start: %lu, Start: %lu, Offset: %lu, "
              "Size: %lu, Partition Type: %d, "
              "Hint: %s\n",
              start, partition->start, partition->volume_offset,
              partition->size, vol->part_type, vol->hint);

            partition_data *entry = malloc(sizeof(partition_data));
            entry->start = start + partition->start;
            entry->offset = partition->volume_offset;
            entry->size = partition->size;
            entry->part_type = vol->part_type;
            list_add(&(entry->list), new_entries);
        }
    }

    return 0;
}

int read_gpt_ldm(int fd, gpt_header *header, gpt_entry **entries,
                 struct list_head *new_entries) {
    privhead *head = NULL;
    uint64_t pt_size;
    int ldm_found = 0;
    int i;
    *entries = NULL;

    // read gpt header
    if (read_gpt_header(fd, header)) {
        goto error;
    }

    // read gpt entries
    pt_size = le32toh(header->num_partition_entries) *
              le32toh(header->sizeof_partition_entry);

    *entries = malloc(pt_size);
    if (!*entries) {
        printf("failed to malloc\n");
        goto error;
    }
    if (read_gpt_entry(fd, header, *entries, pt_size) != 0) {
        printf("failed to read gpt entry\n");
        goto error;
    }

    for (i = 0; i < le32toh(header->num_partition_entries); i++) {
        if (uuid_compare((*entries)[i].type, PARTITION_LDM_METADATA_GUID)) {
            continue;
        }

        if (read_ldm(fd, le64toh((*entries)[i].last_lba), &head)) {
            goto error;
        }

        ldm_found = 1;
    }

    if (!ldm_found) {
        printf("Info: not found ldm info\n");
        goto error;
    }

    if (parse_ldm(be64toh(head->logical_disk_start), new_entries)) {
        goto error;
    }

    if (head) {
        free(head);
    }

    return 0;

error:
    if (head) {
        free(head);
    }

    if (*entries) {
        free(*entries);
        *entries = NULL;
    }

    return -1;
}

int read_mbr_ldm(int fd, struct list_head *new_entries) {
    privhead *head = NULL;

    if (read_ldm(fd, MBR_PRIVHEAD_SECTOR, &head)) {
        goto error;
    }

    if (parse_ldm(be64toh(head->logical_disk_start), new_entries)) {
        goto error;
    }

    if (head) {
        free(head);
    }

    return 0;

error:
    if (head) {
        free(head);
    }

    return -1;
}