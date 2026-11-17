#include <stdint.h>
#include "../include/hellcore_boot_info.h"

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
    void *Functions[4];
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

static HELLCORE_BOOT_INFO g_boot_info;

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b) {
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 ||
        a->Data3 != b->Data3) return 0;
    for (UINTN i = 0; i < 8; ++i) {
        if (a->Data4[i] != b->Data4[i]) return 0;
    }
    return 1;
}

EFI_STATUS efi_main(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable
) {
    (void)ImageHandle;

    serial_init();
    serial_write("\r\nHELLCORE MEMORY HANDOFF START\r\n");

    if (!SystemTable || !SystemTable->BootServices) {
        serial_write("ERROR: UEFI SYSTEM TABLE MISSING\r\n");
        stop();
    }

    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;

    if (!bs->GetMemoryMap || !bs->AllocatePool ||
        !bs->FreePool || !bs->ExitBootServices || !bs->LocateProtocol) {
        serial_write("ERROR: REQUIRED BOOT SERVICE MISSING\r\n");
        stop();
    }

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
            stop();
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
