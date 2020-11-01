#include "bdev.h"

#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_SECTOR_SIZE 512

int bdev_get_sector_size(int fd) {
    int sector_size;
    if (ioctl(fd, BLKSSZGET, &sector_size) < 0) {
        sector_size = DEFAULT_SECTOR_SIZE;
    }

    return sector_size;
}

int bdev_get_size(int fd, uint64_t *bytes) {
#ifdef BLKGETSIZE64
    if (ioctl(fd, BLKGETSIZE64, bytes) >= 0)
        return 0;
#endif /* BLKGETSIZE64 */

#ifdef BLKGETSIZE
    unsigned long size;

    if (ioctl(fd, BLKGETSIZE, &size) >= 0) {
        *bytes = ((uint64_t)size << 9);
        return 0;
    }
#endif /* BLKGETSIZE */

    return -1;
}

int bdev_get_sectors(int fd, uint64_t *sectors) {
    uint64_t bytes;

    if (bdev_get_size(fd, &bytes) == 0) {
        *sectors = bytes / bdev_get_sector_size(fd);
        return 0;
    }

    return -1;
}

int bdev_last_lba(int fd, uint64_t *last_lba) {
    uint64_t sectors;
    if (!bdev_get_sectors(fd, &sectors)) {
        *last_lba = sectors - 1;
        return 0;
    }

    return -1;
}

size_t bdev_read_lba(int fd, uint64_t lba, uint8_t *buffer, size_t count) {
    size_t total_read_count = 0;
    uint64_t sector_size;
    uint64_t last_lba;

    sector_size = bdev_get_sector_size(fd);
    if (bdev_last_lba(fd, &last_lba))
        return 0;

    if (!buffer || lba > last_lba)
        return 0;

    off_t offset = lba * sector_size;
    while (total_read_count < count) {
        ssize_t ret =
            pread(fd, buffer + total_read_count, count - total_read_count,
                  total_read_count + offset);

        if (ret <= 0) {
            return 0;
        }

        total_read_count += ret;
    }

    return total_read_count;
}

size_t bdev_write_lba(int fd, uint64_t lba, uint8_t *buffer, size_t count) {
    size_t total_write_count = 0;
    uint64_t sector_size;
    uint64_t last_lba;

    sector_size = bdev_get_sector_size(fd);
    if (bdev_last_lba(fd, &last_lba))
        return 0;

    if (!buffer || lba > last_lba)
        return 0;

    off_t offset = lba * sector_size;
    while (total_write_count < count) {
        ssize_t ret =
            pwrite(fd, buffer + total_write_count, count - total_write_count,
                   total_write_count + offset);

        if (ret <= 0) {
            printf("Error: failed to write, errno is %d\n", errno);
            return 0;
        }

        total_write_count += ret;
    }

    return total_write_count;
}