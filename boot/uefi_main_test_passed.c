typedef unsigned long long UINT64;
typedef unsigned int UINT32;
typedef unsigned long long EFI_STATUS;
typedef unsigned short CHAR16;
typedef void *EFI_HANDLE;

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    void *Reset;
    EFI_STATUS (*OutputString)(void *This, CHAR16 *String);
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    UINT32 Padding;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
} EFI_SYSTEM_TABLE;

static void outb(unsigned short port, unsigned char value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static unsigned char inb(unsigned short port) {
    unsigned char value;
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
        outb(0x3F8, (unsigned char)*s++);
    }
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle,
                    EFI_SYSTEM_TABLE *SystemTable) {
    (void)ImageHandle;

    serial_init();
    serial_write("HELLCORE EFI ENTRY REACHED\r\n");
    serial_write("Created by DJ Lucifer\r\n");

    static CHAR16 message[] =
        L"HELLCORE UEFI BOOT TEST\r\n"
        L"Created by DJ Lucifer\r\n";

    if (SystemTable && SystemTable->ConOut) {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut, message);
    }

    serial_write("HELLCORE EFI TEST COMPLETE\r\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
