# HELLCORE OS

**Experimental x86-64 operating-system development project.**

HELLCORE focuses on UEFI boot, kernel initialization, physical memory management (PMM), virtual memory management (VMM), and low-level hardware interaction.

> Status: Work in progress. Source code and historical snapshots are present; this repository does not claim a complete or production-ready operating system.

## Development areas

- UEFI boot, kernel loading, memory-map handling, and handoff.
- x86-64 kernel entry, GDT, IDT, exception handling, and diagnostics.
- CORE 0.9 physical memory management and host-test development.
- CORE 1.0 four-level virtual memory management, private page-table ownership, mapping, unmapping, permissions, rollback, and PMM integrity.

## CORE 1.0 VMM V5

Source files:

- `build/core10-vmm/v5/core10_kernel_private_vmm_v5.c`
- `build/core10-vmm/v5/core10_private_vmm_v5.c`
- `build/core10-vmm/v5/core10_private_vmm_v5.h`

V5 is an active development snapshot. Its presence does not establish that it has passed a complete build or runtime validation. V3 and V4 historical snapshots are preserved.

## Repository layout

- `boot/` — UEFI boot and handoff code.
- `include/` — shared boot information.
- `kernel/` — kernel, PMM, VMM, GDT/IDT, and exception code.
- `build/` — development stages and build artifacts.
- `tests/` — test source files.
- `Makefile` — current packaging targets.
- `.gitignore` — generated-file exclusions.

## Build notes

The root `Makefile` currently packages an existing kernel ELF; it is not a complete source-to-ELF build system. The ISO target expects `build/kernel/hellcore.elf` and tools including `objcopy` and `xorriso`.

A successful `make iso` packages the existing ELF; it does not prove a fresh kernel compilation or successful boot.

**Warning:** Do not run `make clean` until its cleanup rule is corrected. It can remove the entire `build/` directory, including tracked source.

## Validation

Report host tests, compilation, QEMU boot tests, and physical-hardware tests separately. Do not claim a test passed unless it was actually executed and recorded.

## Roadmap

1. Review and complete VMM V5.
2. Integrate VMM with the kernel and PMM.
3. Establish a reproducible source-to-ELF build.
4. Validate UEFI boot and kernel handoff.
5. Run host and QEMU integration tests.
6. Expand hardware-driver and storage support.

## Repository

Private repository: https://github.com/mju12162012-lab/HELLCORE

## License

No open-source license has been declared. All rights reserved unless a license is added.
