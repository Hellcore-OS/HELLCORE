# CORE2.0 one-shot limitations and hardware-validation report

## Validation environment

- QEMU emulator version `10.2.1`
- Firmware: installed OVMF `OVMF_CODE.fd` and disposable copies of `OVMF_VARS.fd`
- Acceleration: TCG only; `/dev/kvm` absent
- Guest CPU: `qemu64`
- Guest vCPUs: `1` (single BSP only)
- No physical hardware was available or used.

## Gates deliberately left deferred

### CR3 activation — DEFERRED

Runtime serial evidence shows:

```text
CR3_CHANGED=0
CR3_ACTIVATION=DEFERRED
CR3_GATE_MODEL_OK=1
CR3_GATE_RUNTIME_GATES_OK=0
CR3_GATE_ACTIVATION_AUTHORIZED=0
CR3_GATE_POST_SWITCH_TRANSLATIONS=MODEL_ONLY_NOT_RUNTIME
CR3_GATE_DECISION=DEFERRED_RUNTIME_GATES_UNPROVEN
```

The software model checks page-table ownership, identity coverage, permissions, W^X, supervisor bits, alias conflicts, and controlled-test assumptions. These are model checks, not proof of a safe hardware switch.

Missing runtime proofs:

1. Independently checked virtual-to-physical translation after an actual `mov %cr3`.
2. Exception delivery after the switch using the switched tables.
3. NMI handling and stack/NMI-stack safety after the switch.
4. A validated recovery or controlled fatal-stop path from a post-switch fault.
5. Hardware-observed CR0/CR4/EFER semantics under the final table set.

No CR3 switch was attempted or introduced.

### SMP — DEFERRED

Runtime serial evidence shows:

```text
SMP_PREREQ_GDT_TSS_IST=0
SMP_PREREQ_LAPIC=0
SMP_PREREQ_IOAPIC=0
SMP_PREREQ_LOCKS=0
SMP_PREREQ_COHERENCE=0
SMP_PREREQ_TLB_SHOOTDOWN=0
SMP_PREREQ_MAPPING_OWNER=0
SMP_AP_ENABLED=0
SMP_ONLINE_CPUS_AFTER=1
SMP_GATE=DEFERRED_HARDWARE_PREREQUISITES_UNPROVEN
```

Multi-vCPU execution was not enabled. The host suite `core20_smp` exercises an in-memory coordination model only.

### ECAM — METADATA ONLY

- `machine=pc`: `MCFG_STATUS=ABSENT`; `MG_ECAM_RANGES_RESERVED=0`.
- `machine=q35`: `MCFG_STATUS=OK`, one MCFG entry, segment 0, buses 0x00–0xff, ECAM metadata base `0xE0000000`, aperture `0x10000000`.
- Both machines report `MG_ECAM_ACCESS=METADATA_ONLY_NO_DEREFERENCE`.

No ECAM register was read or written. Metadata discovery does not prove safe UC mapping, ownership, aliasing, or rollback; no ECAM dereference was attempted.

### Cacheability

Both PC and q35 report:

```text
MG_PAT_SUPPORTED=1
MG_MTRR_SUPPORTED=1
MG_MTRR_VARIABLE_NON_WB=0
MG_MMIO_CACHE=UC
MG_MMIO_FLAGS_OK=1
MG_MMIO_UNMAP_OK=1
MG_CACHE_FALLBACKS=0
MG_CACHEABILITY_REJECTS=0
MG_OWNERSHIP_REJECTS_FINAL=0
```

MTRR/PAT inspection and a synthetic MMIO map/unmap lifecycle were exercised. This is not physical-hardware cache-attribute verification.

### Interrupts

Controlled BSP-only paths report `HELLCORE_CONTROLLED_EXCEPTION_DELIVERY_OK=1` and `HELLCORE_CONTROLLED_IRQ0_DELIVERY_OK=1` on PC and q35. These are controlled software-generated delivery paths, not proof of external interrupt-controller hardware support.

## Unsupported analysis / static-analysis boundaries

- Inline assembly: `rdmsr`, `wrmsr`, `cpuid`, `mov cr0`, `mov cr3`, `mov cr4`
- Firmware-provided ACPI/UEFI memory-map structures
- Linker- and assembly-provided symbols
- Cross-translation-unit whole-program rollback behavior
- QEMU TCG timing and hardware side effects
- Physical PCI/PCIe topology and real interrupt routing

## Remaining blockers

1. Physical hardware or `/dev/kvm`-backed execution.
2. Runtime CR3 switch evidence meeting all nine gates.
3. AP startup, per-CPU state, synchronization, interrupt routing, and TLB shootdown before SMP enablement.
4. Proven ECAM UC mapping, ownership, aliasing, rollback, and a safe dereference target.
5. Hardware-observed exception/NMI/TSS/IST behavior.

No blocker was bypassed. No unsafe deferred state was promoted to a claimed capability.
