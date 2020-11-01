#ifndef __MBR_H__
#define __MBR_H__

#include <stdint.h>

#define MSDOS_MBR_SIGNATURE 0xAA55

#define MBR_PART_EFI_PROTECTIVE 0xEE
#define MBR_PART_WINDOWS_LDM 0x42

typedef enum {
    MBR_ERROR_OK = 0,
    MBR_ERROR_READ,
    MBR_ERROR_WRITE,
    MBR_ERROR_INVALID
} mbr_error_t;

typedef struct _mbr_partition {
    uint8_t boot_indicator; /* unused by EFI, set to 0x80 for bootable */
    uint8_t start_head;     /* unused by EFI, pt start in CHS */
    uint8_t start_sector;   /* unused by EFI, pt start in CHS */
    uint8_t start_track;
    uint8_t os_type;       /* EFI and legacy non-EFI OS types */
    uint8_t end_head;      /* unused by EFI, pt end in CHS */
    uint8_t end_sector;    /* unused by EFI, pt end in CHS */
    uint8_t end_track;     /* unused by EFI, pt end in CHS */
    uint32_t starting_lba; /* used by EFI - start addr of the on disk pt */
    uint32_t size_in_lba;  /* used by EFI - size of pt in LBA */
} __attribute__((__packed__)) mbr_partition;

typedef struct _legacy_mbr {
    uint8_t boot_code[440];
    uint32_t unique_mbr_signature;
    uint16_t unknown;
    mbr_partition partition[4];
    uint16_t signature;
} __attribute__((__packed__)) legacy_mbr;

int read_mbr(int fd, legacy_mbr *mbr);
int write_mbr(int fd, legacy_mbr *mbr);

void calcCHS(uint64_t lba, uint8_t *cylinder, uint8_t *heads, uint8_t *sectors);

#endif