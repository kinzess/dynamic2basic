#include <endian.h>
#include <stdio.h>
#include <unistd.h>

#include "bdev.h"
#include "mbr.h"

int read_mbr(int fd, legacy_mbr *mbr) {
    uint32_t i;

    size_t count = 0;
    while (count < sizeof(legacy_mbr)) {
        // ssize_t ret = pread(fd, mbr + count, sizeof(legacy_mbr) - count,
        // count);
        ssize_t ret = bdev_read_lba(fd, 0, (uint8_t *)mbr, sizeof(legacy_mbr));
        if (ret <= 0) {
            printf("Error: failed to read\n");
            return MBR_ERROR_READ;
        }

        count += ret;
    }

    mbr->signature = le16toh(mbr->signature);
    if (mbr->signature != MSDOS_MBR_SIGNATURE) {
        printf("Error: not a valid disk\n");
        return MBR_ERROR_INVALID;
    }

    return MBR_ERROR_OK;
}

int write_mbr(int fd, legacy_mbr *mbr) {
    return bdev_write_lba(fd, 0, (uint8_t *)mbr, sizeof(legacy_mbr));
}

void calcCHS(uint64_t lba, uint8_t *cylinder, uint8_t *heads,
             uint8_t *sectors) {
    if (lba > 1023 * 255 * 63) {
        *cylinder = 0xff;
        *heads = 0xff;
        *sectors = 0xff;
        return;
    }

    int c = lba / (255 * 63);
    int h = (lba / 63) % 255;
    int s = lba % 63;

    *cylinder = c;
    *heads = h;
    *sectors = s;
    *sectors |= (c >> 2) & 0xC0;

    return;
}