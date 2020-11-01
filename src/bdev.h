#ifndef __BDEV_H__
#define __BDEV_H__

#include <linux/fs.h>
#include <stdint.h>
#include <unistd.h>

int bdev_get_sector_size(int fd);
int bdev_get_size(int fd, uint64_t *bytes);
int bdev_get_sectors(int fd, uint64_t *sectors);
int bdev_last_lba(int fd, uint64_t *last_lba);

size_t bdev_read_lba(int fd, uint64_t lba, uint8_t *buffer, size_t count);
size_t bdev_write_lba(int fd, uint64_t lba, uint8_t *buffer, size_t count);

#endif