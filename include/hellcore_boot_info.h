#ifndef HELLCORE_BOOT_INFO_H
#define HELLCORE_BOOT_INFO_H

#include <stdint.h>

#define HELLCORE_BOOT_MAGIC 0x48454C4C434F5245ULL
#define HELLCORE_BOOT_VERSION 2

#define HELLCORE_BOOT_MAX_KERNEL_SEGMENTS 16

#define HELLCORE_ELF_PF_X 1U
#define HELLCORE_ELF_PF_W 2U
#define HELLCORE_ELF_PF_R 4U

typedef struct {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t memory_size;
    uint32_t flags;
    uint32_t reserved;
} HELLCORE_KERNEL_SEGMENT;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t structure_size;

    uint64_t memory_map_address;
    uint64_t memory_map_size;
    uint64_t memory_map_key;
    uint64_t descriptor_size;
    uint32_t descriptor_version;
    uint32_t reserved0;

    uint64_t framebuffer_address;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pixels_per_scanline;
    uint32_t framebuffer_format;

    uint64_t acpi_rsdp_address;
    uint64_t kernel_stack_base;
    uint64_t kernel_stack_size;
    uint64_t pmm_bitmap_address;
    uint64_t pmm_bitmap_size;

    uint32_t kernel_segment_count;
    uint32_t reserved1;

    HELLCORE_KERNEL_SEGMENT
        kernel_segments[HELLCORE_BOOT_MAX_KERNEL_SEGMENTS];

    uint64_t reserved[4];
} HELLCORE_BOOT_INFO;

#endif
