# HELLCORE OS

**Experimental x86-64 operating-system development project.**

HELLCORE is an in-progress operating system focused on UEFI boot,
kernel initialization, physical memory management (PMM), virtual
memory management (VMM), and low-level hardware interaction.

**Status:** Work in progress. HELLCORE is not a complete or
production-ready operating system.

Repository visibility: **Public**

## Project objectives

- UEFI-based x86-64 boot.
- Kernel ELF loading and boot-information handoff.
- Physical memory management.
- Virtual memory management and page-table permissions.
- W^X memory protection.
- Kernel diagnostics and regression testing.
- Future hardware, storage, process, and scheduling subsystems.

## Current engineering status

### CORE2.0 — Kernel and memory-management validation

The CORE2.0 consolidated implementation campaign has a prepared
acceptance package. The package recommends `ACCEPT WITH LIMITATIONS`;
independent acceptance remains pending unless separately recorded.
No independent acceptance decision is claimed here.

These results describe an isolated tested development revision. They
do not establish that the CORE2.0 implementation patch has been merged
into public `main`. This documentation-only update does not apply that
patch: its base does not match public `main`, and its source
prerequisites are not all present there.

| Validation area | Reported result |
|---|---|
| Host regression suites | 15 suites |
| Host assertions | 1,894 `PASS:` assertions plus 63 `driver_phase1` assertions; 0 failures |
| Build validation | 4 builds |
| Checks per build | 107 passed, 0 failed |
| QEMU PC/q35 | 15/15 milestones each; guest shutdown-request path and QEMU exit 0 |
| Warm/cold restart | 10 repetitions each; timeout exit 124 with corrected verifier success |
| Interrupted-init and recovery | Verification passed |
| Preservation | 30 targets checked, 0 changed fields |

The one-shot QEMU rerun observed the guest shutdown-request marker on
PC and q35, but no `SHUTDOWN COMPLETE` marker was observed.

Copied validation reports are in `docs/validation/core20-2026-11-23/`.
The implementation campaign includes work on virtual-memory mapping
and permission policy, page ownership and reverse-map tracking,
transactional allocation/mapping/rollback, W^X and supervisor policy,
cacheability and synthetic MMIO lifecycle tests, ACPI/MCFG discovery,
controlled BSP exception delivery and an IRQ0 path, and restart and
interrupted-initialization recovery.

Explicit limitations remain in force:

- CR3 activation is deferred; post-switch runtime translation, NMI,
  TSS/IST, and recovery proof are incomplete.
- SMP is disabled; single BSP only.
- ECAM access is metadata-only; no ECAM dereference.
- External interrupt routing is not established.
- No physical-hardware validation; QEMU TCG only; `/dev/kvm` absent.
- PC/q35 shutdown-request markers were observed; no shutdown-complete
  marker was observed.

### Isolated integrated software-stack evidence — 2026-11-24

An isolated software-stack reproduction campaign dated 2026-11-24 produced
the following evidence. This record is additive and documentation-only: it
does not apply source patches, merge code into public `main`, change
`LICENSE`, alter CI, or claim HELLCORE OS completion.

| Check | Result |
|---|---|
| Host test suites | 9 |
| Normal host assertions | 289 passed, 0 failed |
| ASan/UBSan host assertions | 289 passed, 0 failed |
| Stage-2 validation | 107 PASS / 0 FAIL per build |
| Campaign seal | `sha256sum -c` 2,310 OK / 0 FAILED |

Stage-2 artifacts were built from isolated source revision
`a3858754bfc0bfa775f6288327dd4ff3da5142e4` (tree
`956639cbab41d703941d982af7a8c86dcfcfcf2f`), not from canonical `HEAD`
or public `main`. The 22 CORE2.x sources remain **UNINTEGRATED** in public
`main`.

Reproduced artifact SHA-256 values, matching the sealed campaign:

| Artifact | SHA-256 |
|---|---|
| `hellcore.elf` | `908b2095c0004a92c61d501e1840318e38d32c9f200e566271a6d7940742e6cf` |
| `hellcore.elf.map` | `78a368e5091a03a3de8159bef7723b0f5d5cc5c43182e4bdb1cb815bdcaf803b` |
| `BOOTX64.EFI` | `686e4c91e75292a37741b1e768b6b6a88573b511841277c0519ca95eb2b4547e` |
| `BOOTX64.EFI.map` | `27d226d64ec6d16fd95acb77508e40215528e457193d88ba28c49ba8d817d5d6` |
| FAT32 ESP image | `c40b4cde4be9999ece4c1d32eae28a38fdf16e32b065ec634188084e39b01385` |
| UEFI ISO image | `48bb89f59dd27aa0e8f6abfdea7e7f75abd1e37dc1d212971354ecc155446778` |
| Stage-2 input manifest | `ed2798d2e09ba72c7b03cfcdc183310f8f37b7d654be90b937ddcd1ebdd45458` |

Campaign seal verification values:

| Item | SHA-256 |
|---|---|
| `CAMPAIGN-SHA256SUMS.txt` | `b6a52994d1df25322492e04c1288602ffd985a87d8a180695f805917b531eab4` |
| `CAMPAIGN-SHA256SUMS.sha256` | `11fac401d612075dd6595aa409b14195f62353244cf9f4698b700379f93cc61a` |

Graphical desktop boot acceptance remains pending. CR3 activation, SMP
enablement, physical-hardware validation, and production readiness are
deferred or unverified. This record claims no acceptance, promotion, boot,
hardware, licensing, or OS-completion result.

### CORE11 — Virtual memory management

CORE11.1 through CORE11.5 have been implemented, host-tested,
checkpointed, and published.

| Milestone | Scope | Recorded result |
|---|---|---|
| CORE11.1 | User/supervisor page permissions | Verified |
| CORE11.2 | NX permission aggregation across paging levels | Verified |
| CORE11.3 | 1 GiB huge-page translation and flags | Verified |
| CORE11.4 | Unambiguous virtual-to-physical translation API | 46 tests passed |
| CORE11.5 | Transactional page-table allocation rollback | 76 tests passed |

The translation API is:

```c
int hellcore_vmm_translate(
    uint64_t virtual_address,
    uint64_t *physical_address
);
```

It returns `1` on success and `0` on failure. A physical address
of zero is not confused with failure.

CORE11.5 makes an individual `hellcore_vmm_map_page()` operation
transactional across its page-table allocation levels. On failure,
it restores journaled entries and releases newly allocated
page-table pages.

This does not establish whole-range rollback for
`hellcore_vmm_map_identity_range()`.

### CORE11 integration work — in progress

The current development branch contains additional changes
under review involving:

- Boot-information ABI v2 initialization and handoff.
- Passing the boot-information structure into the UEFI ELF loader.
- UEFI loader memory and kernel-stack identity mapping.
- PMM bitmap accessibility checks.
- Kernel ELF segment mapping and permission verification.
- VMM address-space construction and mapping validation.

These changes are not yet part of the verified CORE11.1–CORE11.5
milestones. They require source review, reproducible builds,
regression tests, and integration validation before being
reported as complete.

CR3 activation is not established by the current implementation.
The active VMM source currently defers the address-space switch.
Successful CR3 activation and runtime paging remain unverified.

## Test and validation record

Previously recorded results:

- CORE11.5 host regression suite: 76 passed, 0 failed.
- CORE11.4 translation regression suite: 46 passed, 0 failed.
- Tests compile production `kernel/vmm.c` with
  `HELLCORE_VMM_HOST_TEST` and test-only PMM/hardware stubs.
- Strict freestanding x86-64 syntax validation passed.
- Git whitespace validation passed.

These results do not establish successful CR3 activation,
a complete integrated-kernel QEMU boot, or physical-hardware
operation.

Host tests, compilation, emulation, and hardware validation
must be reported separately.

## Verified CORE11 checkpoints

- `checkpoints/core11_1_user_verified_20261118`
- `checkpoints/core11_2_nx_verified_20261118`
- `checkpoints/core11_3_1gib_verified_20261118`
- `checkpoints/core11_4_translate_api_verified_20261118`
- `checkpoints/core11_5_vmm_rollback_verified_20261118`

Checkpoint manifests can be verified with:

```bash
sha256sum -c SHA256SUMS.txt
```

Run the command from the respective checkpoint directory.

## Repository layout

- `boot/` — UEFI boot and handoff code.
- `include/` — shared boot information and interfaces.
- `kernel/` — kernel, PMM, VMM, GDT/IDT, and exception code.
- `tests/` — host-side regression tests.
- `checkpoints/` — verified source and test snapshots.
- `build/` — development stages and build artifacts.
- `Makefile` — current packaging targets.

## Build notes and safety

The root Makefile packages an existing
`build/kernel/hellcore.elf`; it is not a complete
source-to-ELF build system.

The ISO target requires `objcopy` and `xorriso`.

A successful ISO packaging operation does not prove a fresh
kernel compilation or successful boot.

**Warning:** Do not run root `make clean`. Its cleanup rule
removes the entire `build/` directory, which may contain
tracked development source.

Clean individual test binaries only in their test directories.

## Continuous integration

HELLCORE uses GitHub Actions with a Monk CI runner for the
CORE11 host-side regression pipeline.

Workflow: `.github/workflows/hellcore-ci.yml`

The workflow runs on:

- Pull requests targeting `main`.
- Pushes to `main`.
- Manual `workflow_dispatch` requests.

The job is configured with the `monkci-ubuntu-24.04-4`
runner label and installs Clang and Make.

The current pipeline executes four host-side regression suites:

1. CORE11.1 — VMM USER permissions.
2. CORE11.2 — VMM NX permissions.
3. CORE11.4 — VMM translation API.
4. CORE11.5 — VMM transactional rollback.

Regression logs and toolchain version information are uploaded
as the `hellcore-core11-regression-results` workflow artifact.
A successful run of this pipeline has been reported.

**Validation scope:** These are host-side regression tests.
They do not establish a complete freestanding kernel build,
ELF validation, UEFI boot success, CR3 activation, runtime
paging on hardware, or successful hardware operation.

CI success must not be presented as proof of behavior that
the workflow does not test. Future build and boot validation
must use verified, reproducible commands.

## Development roadmap

1. Audit PMM/VMM integration, including initialization order,
   allocation and release contracts, physical-address limits,
   page-table ownership, memory-map handling, and failure paths.
2. Complete ABI v2 boot handoff and VMM integration review.
3. Establish baseline tests and failure-injection coverage.
4. Establish a reproducible source-to-ELF build.
5. Validate kernel initialization, UEFI handoff, and QEMU
   boot behavior.
6. Validate address-space activation and runtime paging.
7. Expand hardware-driver and storage support.

Subsystem readiness must be established through source review
and reproducible tests, not assumed from the completion of
an earlier milestone.

## Repository

Public repository:

https://github.com/Hellcore-OS/HELLCORE

The repository is maintained under the HELLCORE-OS GitHub Organization.

## License

**All Rights Reserved — No Open-Source License.**

Copyright (c) 2026 HELLCORE OS Project.

HELLCORE OS is proprietary software. No license, express or
implied, is granted to use, reproduce, modify, publish,
distribute, sublicense, or sell this project or any portion
of its source code without prior written authorization from
the copyright holder.

See the [LICENSE](LICENSE) file for the full notice.
