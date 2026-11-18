#ifndef HELLCORE_UEFI_ABI_H
#define HELLCORE_UEFI_ABI_H

#if !defined(__x86_64__)
#error "HELLCORE UEFI ABI requires x86-64"
#endif

#if defined(__clang__) || defined(__GNUC__)
#define HELLCORE_EFIAPI __attribute__((ms_abi))
#else
#error "Compiler must support the x86-64 Microsoft ABI attribute"
#endif

#endif
