#include <stdint.h>

typedef uint64_t UINTN;
typedef uint64_t EFI_STATUS;
typedef uint16_t CHAR16;
typedef void *EFI_HANDLE;

#define EFI_SUCCESS 0
#define EFI_OPEN_READ 1

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} EFI_GUID;

typedef EFI_STATUS (*EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE, EFI_GUID *, void **);

typedef struct {
    uint64_t Revision;
    EFI_HANDLE ParentHandle;
    void *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    uint32_t LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    uint64_t ImageSize;
    uint32_t ImageCodeType;
    uint32_t ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (*Open)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, CHAR16 *, uint64_t, uint64_t);
    EFI_STATUS (*Close)(EFI_FILE_PROTOCOL *);
    void *Delete;
    EFI_STATUS (*Read)(EFI_FILE_PROTOCOL *, UINTN *, void *);
    void *Write;
    EFI_STATUS (*GetPosition)(EFI_FILE_PROTOCOL *, uint64_t *);
    EFI_STATUS (*SetPosition)(EFI_FILE_PROTOCOL *, uint64_t);
    void *GetInfo;
    void *SetInfo;
    void *Flush;
};

typedef struct {
    uint64_t Revision;
    EFI_STATUS (*OpenVolume)(void *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    uint64_t Header[3];
    void *Functions[4];
    void *GetMemoryMap;
    void *AllocatePool;
    void *FreePool;
    void *ReservedFunctions[9];
    EFI_HANDLE_PROTOCOL HandleProtocol;
} HELLCORE_BOOT_SERVICES_PREFIX;

static EFI_GUID LOADED_IMAGE_GUID = {
    0x5B1B31A1, 0x9562, 0x11D2,
    {0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}
};

static EFI_GUID SIMPLE_FS_GUID = {
    0x964E5B22, 0x6459, 0x11D2,
    {0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B}
};

static void serial_write(const char *s)
{
    while (*s) {
        uint8_t status;
        __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x3FD));
        if (status & 0x20) {
            __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)*s), "Nd"((uint16_t)0x3F8));
            ++s;
        }
    }
}

EFI_STATUS hellcore_open_kernel_file(
    EFI_HANDLE image,
    HELLCORE_BOOT_SERVICES_PREFIX *bs,
    EFI_FILE_PROTOCOL **kernel_file)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *filesystem = 0;
    EFI_FILE_PROTOCOL *root = 0;

    static CHAR16 path[] = {
        0x005C,0x0045,0x0046,0x0049,0x005C,0x0042,0x004F,0x004F,0x0054,0x005C,
        0x0068,0x0065,0x006C,0x006C,0x0063,0x006F,0x0072,0x0065,0x002E,0x0065,0x006C,0x0066,0
    };

    if (!image || !bs || !bs->HandleProtocol || !kernel_file)
        return 2;

    *kernel_file = 0;

    serial_write("HELLCORE: LOCATING BOOT FILESYSTEM\r\n");

    status = bs->HandleProtocol(
        image, &LOADED_IMAGE_GUID, (void **)&loaded);

    if (status != EFI_SUCCESS || !loaded || !loaded->DeviceHandle)
        return status ? status : 2;

    status = bs->HandleProtocol(
        loaded->DeviceHandle, &SIMPLE_FS_GUID, (void **)&filesystem);

    if (status != EFI_SUCCESS || !filesystem)
        return status ? status : 2;

    status = filesystem->OpenVolume(filesystem, &root);

    if (status != EFI_SUCCESS || !root)
        return status ? status : 2;

    serial_write("HELLCORE: ROOT DIRECTORY OPENED\r\n");

    status = root->Open(
        root, kernel_file, path, EFI_OPEN_READ, 0);

    root->Close(root);

    if (status != EFI_SUCCESS || !*kernel_file) {
        serial_write("HELLCORE: KERNEL FILE NOT FOUND\r\n");
        return status ? status : 2;
    }

    serial_write("HELLCORE: KERNEL ELF FILE OPENED\r\n");
    serial_write("Created by DJ Lucifer\r\n");

    return EFI_SUCCESS;
}

EFI_STATUS hellcore_probe_kernel_file(EFI_HANDLE image, void *boot_services)
{
    EFI_FILE_PROTOCOL *kernel_file = 0;

    EFI_STATUS status = hellcore_open_kernel_file(
        image,
        (HELLCORE_BOOT_SERVICES_PREFIX *)boot_services,
        &kernel_file
    );

    if (status != EFI_SUCCESS)
        return status;

    uint8_t header[64];
    UINTN header_size = sizeof(header);
    status = kernel_file->Read(kernel_file, &header_size, header);

    if (status != EFI_SUCCESS || header_size != sizeof(header)) {
        serial_write("HELLCORE: ELF HEADER READ FAILED\r\n");
        kernel_file->Close(kernel_file);
        return status ? status : 2;
    }

    if (header[0] != 0x7F || header[1] != 0x45 ||
        header[2] != 0x4C || header[3] != 0x46 ||
        header[4] != 2 || header[5] != 1 ||
        header[6] != 1 || header[18] != 0x3E || header[19] != 0) {
        serial_write("HELLCORE: INVALID ELF64 X86-64 HEADER\r\n");
        kernel_file->Close(kernel_file);
        return 2;
    }

    serial_write("HELLCORE: ELF64 HEADER READ SUCCESSFULLY\r\n");
    serial_write("HELLCORE: ELF64 X86-64 VALIDATED\r\n");
    status = kernel_file->Close(kernel_file);

    if (status == EFI_SUCCESS)
        serial_write("HELLCORE: KERNEL FILE CLOSED SUCCESSFULLY\r\n");

    return status;
}


#define HELLCORE_KERNEL_BASE  0x100000ULL
#define HELLCORE_KERNEL_END   0x105000ULL
#define HELLCORE_MAX_PHDRS    16
#define ELF_PT_LOAD           1
#define ELF_PF_X              1
#define ELF_ET_EXEC           2
#define ELF_EM_X86_64         62

typedef struct {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} ELF64_HEADER;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} ELF64_PROGRAM_HEADER;

static int range_inside_kernel(uint64_t address, uint64_t size)
{
    if (address < HELLCORE_KERNEL_BASE ||
        address > HELLCORE_KERNEL_END)
        return 0;

    if (size > HELLCORE_KERNEL_END - address)
        return 0;

    return 1;
}

EFI_STATUS hellcore_load_kernel_elf(
    EFI_HANDLE image,
    void *boot_services,
    uint64_t *entry_address)
{
    if (!entry_address)
        return 2;

    *entry_address = 0;

    EFI_FILE_PROTOCOL *file = 0;

    EFI_STATUS status = hellcore_open_kernel_file(
        image,
        (HELLCORE_BOOT_SERVICES_PREFIX *)boot_services,
        &file);

    if (status != EFI_SUCCESS || !file)
        return status ? status : 2;

    uint64_t file_size = 0;

    status = file->SetPosition(file, UINT64_MAX);
    if (status != EFI_SUCCESS)
        goto fail;

    status = file->GetPosition(file, &file_size);
    if (status != EFI_SUCCESS || file_size < sizeof(ELF64_HEADER)) {
        serial_write("HELLCORE ELF: INVALID FILE SIZE\\r\\n");
        status = 2;
        goto fail;
    }

    status = file->SetPosition(file, 0);
    if (status != EFI_SUCCESS)
        goto fail;

    ELF64_HEADER header;
    UINTN header_size = sizeof(header);

    status = file->SetPosition(file, 0);
    if (status != EFI_SUCCESS)
        goto fail;

    status = file->Read(file, &header_size, &header);
    if (status != EFI_SUCCESS || header_size != sizeof(header))
        goto fail;

    if (header.ident[0] != 0x7F ||
        header.ident[1] != 'E' ||
        header.ident[2] != 'L' ||
        header.ident[3] != 'F' ||
        header.ident[4] != 2 ||
        header.ident[5] != 1 ||
        header.ident[6] != 1 ||
        header.type != ELF_ET_EXEC ||
        header.machine != ELF_EM_X86_64 ||
        header.version != 1 ||
        header.ehsize != sizeof(ELF64_HEADER) ||
        header.phentsize != sizeof(ELF64_PROGRAM_HEADER) ||
        header.phnum == 0 ||
        header.phnum > HELLCORE_MAX_PHDRS) {
        serial_write("HELLCORE ELF: INVALID ELF HEADER\r\n");
        status = 2;
        goto fail;
    }

    uint64_t program_table_size =
        (uint64_t)header.phnum * sizeof(ELF64_PROGRAM_HEADER);

    if (header.phoff > file_size ||
        program_table_size > file_size - header.phoff) {
        serial_write("HELLCORE ELF: PROGRAM TABLE OVERFLOW\r\n");
        status = 2;
        goto fail;
    }

    ELF64_PROGRAM_HEADER phdrs[HELLCORE_MAX_PHDRS];

    status = file->SetPosition(file, header.phoff);
    if (status != EFI_SUCCESS)
        goto fail;

    UINTN table_size =
        (UINTN)header.phnum * sizeof(ELF64_PROGRAM_HEADER);

    status = file->Read(file, &table_size, phdrs);
    if (status != EFI_SUCCESS ||
        table_size != (UINTN)header.phnum *
                      sizeof(ELF64_PROGRAM_HEADER)) {
        serial_write("HELLCORE ELF: PROGRAM TABLE READ FAILED\r\n");
        status = 2;
        goto fail;
    }

    uint32_t load_count = 0;
    int entry_valid = 0;

    /*
     * Validate every loadable segment before modifying kernel memory.
     */
    for (uint16_t i = 0; i < header.phnum; ++i) {
        ELF64_PROGRAM_HEADER *ph = &phdrs[i];

        if (ph->type != ELF_PT_LOAD)
            continue;

        if (ph->filesz > ph->memsz ||
            ph->memsz == 0 ||
            ph->offset > file_size ||
            ph->filesz > file_size - ph->offset ||
            ph->paddr > UINT64_MAX - ph->memsz ||
            !range_inside_kernel(ph->paddr, ph->memsz)) {
            serial_write("HELLCORE ELF: INVALID LOAD SEGMENT BOUNDS\r\n");
            status = 2;
            goto fail;
        }

        if (ph->align > 1) {
            if ((ph->align & (ph->align - 1)) != 0 ||
                (ph->vaddr % ph->align) !=
                (ph->offset % ph->align)) {
                serial_write("HELLCORE ELF: INVALID SEGMENT ALIGNMENT\r\n");
                status = 2;
                goto fail;
            }
        }

        if ((ph->flags & ELF_PF_X) &&
            header.entry >= ph->vaddr &&
            header.entry - ph->vaddr < ph->memsz) {
            entry_valid = 1;
        }

        for (uint16_t j = 0; j < i; ++j) {
            ELF64_PROGRAM_HEADER *previous = &phdrs[j];

            if (previous->type != ELF_PT_LOAD)
                continue;

            uint64_t previous_end =
                previous->paddr + previous->memsz;
            uint64_t current_end =
                ph->paddr + ph->memsz;

            if (ph->paddr < previous_end &&
                previous->paddr < current_end) {
                serial_write("HELLCORE ELF: OVERLAPPING SEGMENTS\r\n");
                status = 2;
                goto fail;
            }
        }

        ++load_count;
    }

    if (load_count == 0 || !entry_valid) {
        serial_write("HELLCORE ELF: INVALID LOAD COUNT OR ENTRY\r\n");
        status = 2;
        goto fail;
    }

    serial_write("HELLCORE ELF: PROGRAM HEADERS VALIDATED\r\n");

    /*
     * Load file-backed bytes and zero each segment's BSS.
     */
    for (uint16_t i = 0; i < header.phnum; ++i) {
        ELF64_PROGRAM_HEADER *ph = &phdrs[i];

        if (ph->type != ELF_PT_LOAD)
            continue;

        status = file->SetPosition(file, ph->offset);
        if (status != EFI_SUCCESS)
            goto fail;

        uint8_t *destination = (uint8_t *)(uintptr_t)ph->paddr;
        UINTN remaining = (UINTN)ph->filesz;

        status = file->Read(file, &remaining, destination);
        if (status != EFI_SUCCESS ||
            remaining != (UINTN)ph->filesz) {
            serial_write("HELLCORE ELF: SEGMENT READ FAILED\r\n");
            status = 2;
            goto fail;
        }

        for (uint64_t j = ph->filesz; j < ph->memsz; ++j)
            destination[j] = 0;
    }

    *entry_address = header.entry;

    serial_write("HELLCORE ELF: LOADABLE SEGMENTS COPIED\r\n");
    serial_write("HELLCORE ELF: BSS ZEROED\r\n");
    serial_write("HELLCORE ELF: ENTRY POINT VALIDATED\r\n");

    status = file->Close(file);
    if (status == EFI_SUCCESS)
        serial_write("HELLCORE ELF: FILE CLOSED\r\n");

    return status;

fail:
    file->Close(file);
    return status ? status : 2;
}
