#ifndef HELLCORE_BOOT_INFO_H
#define HELLCORE_BOOT_INFO_H

#include <stdint.h>

#define HELLCORE_BOOT_MAGIC 0x48454C4C434F5245ULL
#define HELLCORE_BOOT_VERSION 1

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
    uint64_t reserved[4];
} HELLCORE_BOOT_INFO;

#endif
