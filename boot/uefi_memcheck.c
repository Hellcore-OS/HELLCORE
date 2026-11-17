#include <stdint.h>
#include "../include/hellcore_boot_info.h"
#include "uefi_pmm_reserve.h"

typedef uint64_t UINTN;
typedef uint64_t EFI_STATUS;
typedef uint32_t UINT32;
typedef uint16_t CHAR16;
typedef void *EFI_HANDLE;

#define EFI_SUCCESS             0
#define EFI_INVALID_PARAMETER   (0x8000000000000000ULL | 2)
#define EFI_BUFFER_TOO_SMALL    (0x8000000000000000ULL | 5)
#define EFI_LOADER_DATA         2

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    uint32_t Type;
    uint32_t Padding;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct {
    void *Reset;
    EFI_STATUS (*OutputString)(void *, CHAR16 *);
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS (*EFI_ALLOCATE_PAGES)(
    UINT32, UINT32, UINTN, uint64_t *);

typedef EFI_STATUS (*EFI_GET_MEMORY_MAP)(
    UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *, UINTN *, UINT32 *);

typedef EFI_STATUS (*EFI_ALLOCATE_POOL)(
    UINT32, UINTN, void **);

typedef EFI_STATUS (*EFI_FREE_POOL)(void *);

typedef EFI_STATUS (*EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE, UINTN);

typedef EFI_STATUS (*EFI_LOCATE_PROTOCOL)(
    void *, void *, void **);

typedef struct { uint32_t Data1; uint16_t Data2; uint16_t Data3; uint8_t Data4[8]; } EFI_GUID;

typedef struct {
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;
typedef struct { uint32_t Version; uint32_t HorizontalResolution; uint32_t VerticalResolution; uint32_t PixelFormat; uint32_t PixelInformation[4]; uint32_t PixelsPerScanLine; } EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct { uint32_t MaxMode; uint32_t Mode; EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info; UINTN SizeOfInfo; uint64_t FrameBufferBase; UINTN FrameBufferSize; } EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct { void *QueryMode; void *SetMode; void *Blt; EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode; } EFI_GRAPHICS_OUTPUT_PROTOCOL;
static EFI_GUID GOP_GUID = { 0x9042A9DE, 0x23DC, 0x4A38, {0x96,0xFB,0x7A,0xDE,0xD0,0x80,0x51,0x6A} };
static EFI_GUID ACPI_20_GUID = { 0x8868E871, 0xE4F1, 0x11D3, {0xBC,0x22,0x00,0x80,0xC7,0x3C,0x88,0x81} };
static EFI_GUID ACPI_10_GUID = { 0xEB9D2D30, 0x2D88, 0x11D3, {0x9A,0x16,0x00,0x90,0x27,0x3F,0xC1,0x4D} };

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    void *FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void *ReservedFunctions[19];
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *ReservedAfterExit[10];
    EFI_LOCATE_PROTOCOL LocateProtocol;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    UINT32 Padding;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    void *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

static void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}

static void serial_write(const char *s) {
    while (*s) {
        while (!(inb(0x3FD) & 0x20)) {}
        outb(0x3F8, (uint8_t)*s++);
    }
}

static void stop(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}


static void serial_hex(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int i = 60; i >= 0; i -= 4) {
        char c[2] = { digits[(value >> i) & 15], 0 };
        serial_write(c);
    }
}


static void inspect_kernel_memory(
    EFI_MEMORY_DESCRIPTOR *map,
    UINTN map_size,
    UINTN descriptor_size
) {
    const uint64_t target_start = 0x100000;
    const uint64_t target_end = 0x110000;
    int conventional_cover = 0;
    int loader_cover = 0;

    serial_write("\r\nHELLCORE PHYSICAL MEMORY INSPECTION\r\n");

    if (descriptor_size == 0 ||
        descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        serial_write("HELLCORE ERROR: INVALID DESCRIPTOR SIZE\r\n");
        return;
    }

    for (UINTN offset = 0;
         offset <= map_size &&
         sizeof(EFI_MEMORY_DESCRIPTOR) <= map_size - offset;
         offset += descriptor_size) {

        EFI_MEMORY_DESCRIPTOR *d =
            (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)map + offset);

        if (d->NumberOfPages > UINT64_MAX / 4096)
            continue;

        uint64_t bytes = d->NumberOfPages * 4096;

        if (d->PhysicalStart > UINT64_MAX - bytes)
            continue;

        uint64_t end = d->PhysicalStart + bytes;

        if (d->PhysicalStart < target_end && end > target_start) {
            serial_write("OVERLAPPING DESCRIPTOR TYPE=");
            serial_hex(d->Type);
            serial_write(" START=");
            serial_hex(d->PhysicalStart);
            serial_write(" END=");
            serial_hex(end);
            serial_write("\r\n");

            if (d->PhysicalStart <= target_start &&
                end >= target_end) {
                if (d->Type == 7)
                    conventional_cover = 1;
                if (d->Type == EFI_LOADER_DATA)
                    loader_cover = 1;
            }
        }

        if (map_size - offset < descriptor_size)
            break;
    }

    if (loader_cover)
        serial_write("HELLCORE TARGET RANGE RESERVED AS LOADER DATA\r\n");
    else if (conventional_cover)
        serial_write("HELLCORE TARGET RANGE COVERED BY CONVENTIONAL MEMORY\r\n");
    else
        serial_write("HELLCORE TARGET RANGE NOT CONFIRMED\r\n");
}

static HELLCORE_BOOT_INFO g_boot_info;

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b) {
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 ||
        a->Data3 != b->Data3) return 0;
    for (UINTN i = 0; i < 8; ++i) {
        if (a->Data4[i] != b->Data4[i]) return 0;
    }
    return 1;
}

EFI_STATUS hellcore_probe_kernel_file(EFI_HANDLE, void *);
EFI_STATUS hellcore_load_kernel_elf(EFI_HANDLE, void *, uint64_t *);
__attribute__((noreturn)) void hellcore_enter_kernel(HELLCORE_BOOT_INFO *, uint64_t, uint64_t, uint64_t);

EFI_STATUS efi_main(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable
) {

    serial_init();
    serial_write("\r\nHELLCORE MEMORY HANDOFF START\r\n");

    if (!SystemTable || !SystemTable->BootServices) {
        serial_write("ERROR: UEFI SYSTEM TABLE MISSING\r\n");
        stop();
    }

    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;

    if (!bs->AllocatePages || !bs->GetMemoryMap ||
        !bs->AllocatePool || !bs->FreePool ||
        !bs->ExitBootServices || !bs->LocateProtocol) {
        serial_write("ERROR: REQUIRED BOOT SERVICE MISSING\r\n");
        stop();
    }

    EFI_STATUS fs_status = hellcore_probe_kernel_file(ImageHandle, bs);
    if (fs_status != EFI_SUCCESS) {
        serial_write("HELLCORE FILESYSTEM PROBE FAILED\r\n");
        stop();
    }
    serial_write("HELLCORE FILESYSTEM PROBE PASSED\r\n");

    g_boot_info.acpi_rsdp_address = 0;

    if (SystemTable->ConfigurationTable) {
        for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; ++i) {
            EFI_CONFIGURATION_TABLE *entry = &SystemTable->ConfigurationTable[i];

            if (guid_equal(&entry->VendorGuid, &ACPI_20_GUID)) {
                g_boot_info.acpi_rsdp_address =
                    (uint64_t)(uintptr_t)entry->VendorTable;
                serial_write("HELLCORE ACPI 2.0 RSDP FOUND\r\n");
                break;
            }
        }

        if (!g_boot_info.acpi_rsdp_address) {
            for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; ++i) {
                EFI_CONFIGURATION_TABLE *entry = &SystemTable->ConfigurationTable[i];

                if (guid_equal(&entry->VendorGuid, &ACPI_10_GUID)) {
                    g_boot_info.acpi_rsdp_address =
                        (uint64_t)(uintptr_t)entry->VendorTable;
                    serial_write("HELLCORE ACPI 1.0 RSDP FOUND\r\n");
                    break;
                }
            }
        }
    }

    if (!g_boot_info.acpi_rsdp_address) {
        serial_write("WARNING: ACPI RSDP NOT FOUND\r\n");
    }

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS gop_status = bs->LocateProtocol(
        &GOP_GUID, 0, (void **)&gop);

    if (gop_status == EFI_SUCCESS && gop &&
        gop->Mode && gop->Mode->Info) {
        g_boot_info.framebuffer_address = gop->Mode->FrameBufferBase;
        g_boot_info.framebuffer_size = gop->Mode->FrameBufferSize;
        g_boot_info.framebuffer_width = gop->Mode->Info->HorizontalResolution;
        g_boot_info.framebuffer_height = gop->Mode->Info->VerticalResolution;
        g_boot_info.framebuffer_pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
        g_boot_info.framebuffer_format = gop->Mode->Info->PixelFormat;
        serial_write("HELLCORE GOP FRAMEBUFFER CAPTURED\r\n");
    } else {
        serial_write("WARNING: GOP NOT AVAILABLE\r\n");
    }

    uint64_t kernel_base = 0x100000;
    EFI_STATUS kernel_alloc_status =
        bs->AllocatePages(2, EFI_LOADER_DATA, 16, &kernel_base);

    if (kernel_alloc_status != EFI_SUCCESS) {
        serial_write("HELLCORE KERNEL RESERVATION FAILED STATUS=");
        serial_hex(kernel_alloc_status);
        serial_write("\r\n");
        stop();
    }

    if (kernel_base != 0x100000) {
        serial_write("HELLCORE KERNEL ADDRESS MISMATCH\r\n");
        stop();
    }

    serial_write("HELLCORE KERNEL MEMORY RESERVED AT ");
    serial_hex(kernel_base);
    serial_write(" SIZE=0x10000\r\n");

    uint64_t kernel_entry = 0;
    EFI_STATUS load_status =
        hellcore_load_kernel_elf(ImageHandle, bs, &kernel_entry);

    if (load_status != EFI_SUCCESS) {
        serial_write("HELLCORE KERNEL ELF LOAD FAILED STATUS=");
        serial_hex(load_status);
        serial_write("\r\n");
        stop();
    }

    if (kernel_entry != 0x101000) {
        serial_write("HELLCORE KERNEL ENTRY ADDRESS MISMATCH=");
        serial_hex(kernel_entry);
        serial_write("\r\n");
        stop();
    }

    serial_write("HELLCORE KERNEL ELF LOADED AT ");
    serial_hex(kernel_entry);
    serial_write("\r\n");


    /* Reserve a dedicated 64 KiB kernel stack before capturing the final memory map. */
    uint64_t kernel_stack_base = 0;
    EFI_STATUS stack_status = bs->AllocatePages(0, EFI_LOADER_DATA, 16, &kernel_stack_base);
    if (stack_status != EFI_SUCCESS || !kernel_stack_base) {
        serial_write("HELLCORE KERNEL STACK ALLOCATION FAILED STATUS=");
        serial_hex(stack_status);
        serial_write("\r\n");
        stop();
    }
    g_boot_info.kernel_stack_base = kernel_stack_base;
    g_boot_info.kernel_stack_size = 16 * 4096;
    serial_write("HELLCORE KERNEL STACK RESERVED AT ");
    serial_hex(kernel_stack_base);
    serial_write(" SIZE=0x10000\r\n");

    uint64_t pmm_bitmap_address = 0;
    uint64_t pmm_bitmap_size = 0;
    serial_write("HELLCORE PMM: RESERVING BITMAP STORAGE\r\n");
    if (!hellcore_uefi_reserve_pmm_bitmaps(
        bs->AllocatePages,
        (HELLCORE_GET_MEMORY_MAP)bs->GetMemoryMap,
        bs->AllocatePool,
        bs->FreePool,
        &pmm_bitmap_address,
        &pmm_bitmap_size)) {
        serial_write("ERROR: PMM BITMAP RESERVATION FAILED\r\n");
        stop();
    }
    g_boot_info.pmm_bitmap_address = pmm_bitmap_address;
    g_boot_info.pmm_bitmap_size = pmm_bitmap_size;
    serial_write("HELLCORE PMM BITMAP ADDRESS: ");
    serial_hex(pmm_bitmap_address);
    serial_write("\r\nHELLCORE PMM BITMAP SIZE: ");
    serial_hex(pmm_bitmap_size);
    serial_write("\r\nHELLCORE PMM BITMAP STORAGE RESERVED\r\n");

    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;

    EFI_STATUS status = bs->GetMemoryMap(
        &map_size,
        0,
        &map_key,
        &descriptor_size,
        &descriptor_version
    );

    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size == 0) {
        serial_write("ERROR: MEMORY MAP SIZE QUERY FAILED\r\n");
        stop();
    }

    void *map_buffer = 0;

    /*
     * Allocate extra room because the map can grow while
     * the EFI application is running.
     */
    for (unsigned int attempt = 0; attempt < 4; ++attempt) {
        UINTN allocation_size =
            map_size + (descriptor_size * 16);

        status = bs->AllocatePool(
            EFI_LOADER_DATA,
            allocation_size,
            &map_buffer
        );

        if (status != EFI_SUCCESS || !map_buffer) {
            serial_write("ERROR: MEMORY MAP BUFFER ALLOCATION FAILED\r\n");
            stop();
        }

        map_size = allocation_size;

        status = bs->GetMemoryMap(
            &map_size,
            (EFI_MEMORY_DESCRIPTOR *)map_buffer,
            &map_key,
            &descriptor_size,
            &descriptor_version
        );

        if (status == EFI_SUCCESS)
            break;

        bs->FreePool(map_buffer);
        map_buffer = 0;

        if (status != EFI_BUFFER_TOO_SMALL) {
            serial_write("ERROR: MEMORY MAP RETRIEVAL FAILED\r\n");
            stop();
        }
    }

    if (!map_buffer || status != EFI_SUCCESS) {
        serial_write("ERROR: COULD NOT CAPTURE MEMORY MAP\r\n");
        stop();
    }

    inspect_kernel_memory((EFI_MEMORY_DESCRIPTOR *)map_buffer, map_size, descriptor_size);

    g_boot_info.magic = HELLCORE_BOOT_MAGIC;
    g_boot_info.version = HELLCORE_BOOT_VERSION;
    g_boot_info.structure_size = sizeof(HELLCORE_BOOT_INFO);

    g_boot_info.memory_map_address =
        (uint64_t)(uintptr_t)map_buffer;
    g_boot_info.memory_map_size = map_size;
    g_boot_info.memory_map_key = map_key;
    g_boot_info.descriptor_size = descriptor_size;
    g_boot_info.descriptor_version = descriptor_version;



    serial_write("HELLCORE MEMORY MAP CAPTURED\r\n");
    serial_write("HELLCORE BOOT INFO STRUCTURE READY\r\n");
    serial_write("HELLCORE EXITING UEFI BOOT SERVICES\r\n");

    /*
     * No more allocations or other boot-service calls between
     * GetMemoryMap and ExitBootServices.
     */
    for (unsigned int retry = 0; retry < 4; ++retry) {
        status = bs->ExitBootServices(ImageHandle, map_key);

        if (status == EFI_SUCCESS) {
            serial_write("HELLCORE EXIT BOOT SERVICES SUCCESS\r\n");
            serial_write("HELLCORE BOOT INFO HANDOFF READY\r\n");
            serial_write("Created by DJ Lucifer\r\n");
            hellcore_enter_kernel(&g_boot_info, kernel_entry, g_boot_info.kernel_stack_base, g_boot_info.kernel_stack_size);
        }
        if (status != EFI_INVALID_PARAMETER) {
            serial_write("ERROR: EXIT BOOT SERVICES FAILED\r\n");
            stop();
        }

        /*
         * The key became stale. Refresh the map without
         * allocating or freeing memory, then retry.
         */
        status = bs->GetMemoryMap(
            &map_size,
            (EFI_MEMORY_DESCRIPTOR *)map_buffer,
            &map_key,
            &descriptor_size,
            &descriptor_version
        );

        if (status != EFI_SUCCESS) {
            serial_write("ERROR: MEMORY MAP REFRESH FAILED\r\n");
            stop();
        }

        g_boot_info.memory_map_size = map_size;
        g_boot_info.memory_map_key = map_key;
        g_boot_info.descriptor_size = descriptor_size;
        g_boot_info.descriptor_version = descriptor_version;
    }

    serial_write("ERROR: EXIT BOOT SERVICES RETRIES EXHAUSTED\r\n");
    stop();

    return EFI_SUCCESS;
}
