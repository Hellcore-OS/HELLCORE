# HELLCORE CORE2.0 — one-shot runtime verification and limitation closure

Generated: `2026-11-23T22:40Z`
Workspace reused: `/data/data/com.termux/files/usr/tmp/hellcore-consolidated-closure-20261123T210838Z` (`$W`)
New campaign log root: `$W/build/core20/oneshot-20261123T222646Z/logs`
Canonical repository: `$HOME/HELLCORE` (read-only)

This was an implementation-and-validation campaign. It reused the existing consolidated workspace and build tooling; it did not create another full repository copy or duplicate build tree. All new outputs use new labels under the existing workspace.

---

## 1. Original baseline and tested revision

The latest consolidated CORE2.0 implementation was located in `$W` and verified before any new work:

| Evidence | SHA-256 / result |
|---|---|
| Tested-source manifest | `0dfe8216f60c89305246dee00a31b9dcd30a4b94a4462912854b9edf07a30a4f` |
| Original consolidated patch | `20db79acc54e42e1248b72f66438705fb1213c222343bc0722b2f5b0627ca181` |
| Original deliverables manifest | `86cda86dadd9dacb9d944bebfc179d795d21db2948763a5265153198826b9f01` |
| Source manifest verification | `rc=0`, `313/313 OK` |
| Deliverables manifest verification | `rc=0`, `31/31 OK` |
| Original patch dry-run against historical base | `rc=0` |
| Patch reconstruction match | `36/36` paths, `0` missing, `0` mismatched |

Exact tested revision:

- Git HEAD: `a3858754bfc0bfa775f6288327dd4ff3da5142e4`
- Git TREE: `956639cbab41d703941d982af7a8c86dcfcfcf2f`
- 313 canonical source inputs in the tested-source manifest
- Source staged entries: `0`
- Source porcelain entries: `132` (local untracked helper/build artifacts in the isolated workspace)

The current testing source matched the reported tested revision exactly. The historical 1,894-assertion and QEMU evidence is linked to this revision.

---

## 2. Exact source changes and reasons

No source changes were made in this campaign.

The one-shot patch, `patches/CORE20-ONESHOT-CLOSURE.patch`, is a byte-identical copy of the original consolidated patch under a new filename. Its SHA-256 is `20db79acc54e42e1248b72f66438705fb1213c222343bc0722b2f5b0627ca181`.

The original 36-path patch covers:

- shutdown capability flags and warm/cold reset variants;
- ACPI/MCFG discovery, validation, and ECAM metadata policy;
- CR3 gate model and reporting;
- controlled exception and BSP IRQ0 paths;
- SMP coordination model and deferred gate;
- VMM/MEMGUARD reverse-map, ownership, cacheability, and rollback support;
- new/extended host suites and verification tools;
- reproducible build-script updates.

These changes were inherited unchanged from the already implemented and accepted-with-limitations CORE2.0 revision. No unrelated driver, scheduler, desktop, graphics, Forge, or Quantum Core change was made.

---

## 3. Defects found and fixes applied

### Product/source defects

None. All host suites, fresh builds, QEMU runs, restart variants, and recovery runs passed after correct evidence interpretation. No source fix, rebuild-after-fix, or new regression test was required.

### Evidence/harness issues resolved without source change

1. **Cold restart verifier marker mismatch.** The first cold verifier invocation used the warm-restart default marker and exited `1`, although the serial log showed 10 `HELLCORE GUEST COLD RESTART REQUESTED` markers. The verifier supports `RESTART_MARKER`; rerunning with the cold marker exited `0`. The original failed output was preserved as `RESTART-VERIFICATION.txt`; the corrected output is `RESTART-VERIFICATION-COLD.txt`.

2. **Preservation artifact relocation.** An earlier Clang analyzer checker-help probe had created an empty `null.plist` in the stage2-correction accepted workspace. It was moved into the one-shot logs as `CLANG-ANALYZER-HELP-null.plist`, restoring the accepted workspace to its measured baseline. No HELLCORE source, configuration, report, or checkpoint file was touched.

No failed test was suppressed and no timeout was converted into success. The cold verifier correction was a documented invocation correction based on explicit cold-boot evidence.

---

## 4. Host test totals and failures

Exact command:

```sh
bash "$W/tools/core20_run_host_tests.sh" oneshot-final1
```

Exit: `0`
Log: `$W/build/core20/host-tests/oneshot-final1/logs/HOST-TEST-RESULTS.txt`

| Metric | Value |
|---|---:|
| Suites executed | 15 |
| Suites with nonzero exit | 0 |
| `PASS:` assertions | 1,894 |
| `FAIL:` assertions | 0 |
| `driver_phase1` assertions | 63 |
| `driver_phase1` failures | 0 |

Per-suite results:

| Suite | Exit | `PASS:` |
|---|---:|---:|
| `core10_vmm_verify` | 0 | 50 |
| `core11_1_vmm_user` | 0 | 14 |
| `core11_2_vmm_nx` | 0 | 33 |
| `core11_4_vmm_translate` | 0 | 46 |
| `core11_5_vmm_rollback` | 0 | 76 |
| `step2_stage1_rmap` | 0 | 72 |
| `step2_stage2_rollback` | 0 | 1,012 |
| `step3_teardown` | 0 | 154 |
| `step4_safety` | 0 | 64 |
| `driver_phase1` | 0 | 63 assertions, 0 failures |
| `core20_memguard` | 0 | 145 |
| `core20_interrupts` | 0 | 29 |
| `core20_cr3_gate` | 0 | 37 |
| `core20_smp` | 0 | 143 |
| `limitation_closure` | 0 | 19 |

Coverage exercised includes VMM init/teardown, reverse-map accounting, supervisor/W^X/mapping policy, replacement and alias conflicts, multi-page map/unmap, rollback after allocation failure, parent permission restoration, reclamation, repeated init/teardown, allocation exhaustion recovery, cacheability conflicts, synthetic MMIO lifecycle, interrupts, CR3 gate model, SMP model, MCFG validation, and reset variants.

---

## 5. Build results and reproducibility

Four fresh builds were run with the existing reproducible build script. No Makefile target, root `clean`, build in the canonical repository, or duplicate source tree was used.

| Build label | Extra kernel flags | Exit | Validation PASS | FAIL | Warnings | Errors |
|---|---|---:|---:|---:|---:|---:|
| `kernel-oneshot-final1` | none | 0 | 107 | 0 | 0 | 0 |
| `kernel-oneshot-final1r` | none | 0 | 107 | 0 | 0 | 0 |
| `kernel-oneshot-restart-warm` | `-DHELLCORE_DEFAULT_RESET_TYPE=1` | 0 | 107 | 0 | 0 | 0 |
| `kernel-oneshot-restart-cold` | `-DHELLCORE_DEFAULT_RESET_TYPE=0` | 0 | 107 | 0 | 0 | 0 |

Artifact hashes:

| Build | `hellcore.elf` SHA-256 | `BOOTX64.EFI` SHA-256 |
|---|---|---|
| `kernel-oneshot-final1` | `0765cf9005c463b35d20b66c5ffa96d7f69a73f6295e6fd932fbcf8d88b78b0e` | `686e4c91e75292a37741b1e768b6b6a88573b511841277c0519ca95eb2b4547e` |
| `kernel-oneshot-final1r` | `0765cf9005c463b35d20b66c5ffa96d7f69a73f6295e6fd932fbcf8d88b78b0e` | `686e4c91e75292a37741b1e768b6b6a88573b511841277c0519ca95eb2b4547e` |
| `kernel-oneshot-restart-warm` | `b585c7eaa73c6c79f73dacbaf27ef7db6a0c08c6eda3783797e0f8c1202a9cfa` | `686e4c91e75292a37741b1e768b6b6a88573b511841277c0519ca95eb2b4547e` |
| `kernel-oneshot-restart-cold` | `2d4c73d9f81c8cecfb2fadfcfacecce4046dafa96d960dc04d7c979931613d8b` | `686e4c91e75292a37741b1e768b6b6a88573b511841277c0519ca95eb2b4547e` |

Reproducibility:

- fresh rebuild ELF identical: `yes`
- fresh rebuild EFI identical: `yes`
- new final ELF identical to historical `kernel-consolidated-final1`: `yes`
- new final EFI identical to historical `kernel-consolidated-final1`: `yes`
- warm/cold EFI identical: `yes` (ELF differs only by reset-type definition)

---

## 6. QEMU PC and q35 results

QEMU: `/data/data/com.termux/files/usr/bin/qemu-system-x86_64`, version `10.2.1`. Firmware: installed OVMF. `/dev/kvm` absent. All runs used `-accel tcg -cpu qemu64 -smp 1 -m 256M -display none -serial stdio -monitor none -no-reboot -net none` for normal shutdown runs.

Wrapper commands:

```sh
BUILD="$W/build/core20/kernel-oneshot-final1" MACHINE=pc  bash "$W/tools/core20_run_qemu.sh" oneshot-final-pc 90
BUILD="$W/build/core20/kernel-oneshot-final1" MACHINE=q35 bash "$W/tools/core20_run_qemu.sh" oneshot-final-q35 90
```

Expanded PC command from `evidence/core20/qemu/oneshot-final-pc/COMMAND.txt`:

```sh
timeout 90s /data/data/com.termux/files/usr/bin/qemu-system-x86_64 \
  -machine pc -accel tcg -cpu qemu64 -smp 1 -m 256M \
  -display none -serial stdio -monitor none -no-reboot -net none \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/data/data/com.termux/files/usr/share/edk2/ovmf/OVMF_CODE.fd \
  -drive if=pflash,format=raw,unit=1,file=$W/build/core20/qemu/oneshot-final-pc/OVMF_VARS.fd \
  -drive file=fat:rw:$W/build/core20/qemu/oneshot-final-pc/esp,if=ide,index=0,media=disk
```

The q35 command is identical except `-machine q35` and the `oneshot-final-q35` run paths.

| Result | PC | q35 |
|---|---|---|
| Driver exit | 0 | 0 |
| `timeout_wrapper_exit` | 0 | 0 |
| External timeout | no | no |
| Milestones reached | 15/15 | 15/15 |
| Classification | `KERNEL_INITIALIZATION_COMPLETE_QEMU_EXIT_ZERO` | `KERNEL_INITIALIZATION_COMPLETE_QEMU_EXIT_ZERO` |
| Clean guest-driven shutdown marker | `HELLCORE GUEST SHUTDOWN REQUESTED=1` | `HELLCORE GUEST SHUTDOWN REQUESTED=1` |
| Fault/panic markers | none | none |
| Duplicate/contradictory result markers | none detected | none detected |
| Immutable input hashes before/after | unchanged | unchanged |

Guest result markers present exactly once on each machine: `HELLCORE KERNEL ENTRY REACHED`, `HELLCORE PMM: INVENTORY COMPLETE`, `HELLCORE VMM: PAGE TABLES BUILT AND VERIFIED`, `VMM_ROOT=0x0000000000360000`, `HELLCORE KERNEL INITIALIZATION COMPLETE`, `HELLCORE_CONTROLLED_EXCEPTION_DELIVERY_OK=1`, `HELLCORE_CONTROLLED_IRQ0_DELIVERY_OK=1`, `CR3_CHANGED=0`, `CR3_ACTIVATION=DEFERRED`, `SMP_GATE=DEFERRED_HARDWARE_PREREQUISITES_UNPROVEN`.

QEMU exit `0` was produced by the guest shutdown path in 9 s (PC) and 8 s (q35), not by external timeout. Exit `124` was never relabeled as success.

---

## 7. Exception and interrupt results

Both PC and q35 report:

```text
HELLCORE_CONTROLLED_EXCEPTION_DELIVERY_OK=1
HELLCORE_CONTROLLED_IRQ0_DELIVERY_OK=1
```

Host suite `core20_interrupts` passed 29/29 assertions covering PIC remap/mask/EOI planning, PIT divisor planning, bounded STI/CLI window, and invalid-input rejection.

These are controlled BSP-only software paths. They do not claim external interrupt-controller hardware support.

---

## 8. Restart and recovery results

### Warm restart

Command:

```sh
env BUILD="$W/build/core20/kernel-oneshot-restart-warm" \
  RESTART_VARIANT=WARM MACHINE=pc \
  bash "$W/tools/core20_run_qemu_restart.sh" oneshot-restart-warm 90
```

Exit: `0`; `timeout_wrapper_exit=124` (expected); verifier exit `0`.
Observed 10 repeated boots with `HELLCORE GUEST WARM RESTART REQUESTED`, 10 PMM/VMM initializations, 10 kernel completions, no fault markers, no shutdown marker, `CR3_GATE_MODEL_OK=1` x10, `SMP_AP_ENABLED=0` x10.

### Cold restart

Command:

```sh
env BUILD="$W/build/core20/kernel-oneshot-restart-cold" \
  RESTART_VARIANT=COLD MACHINE=pc \
  bash "$W/tools/core20_run_qemu_restart.sh" oneshot-restart-cold 90
```

Runtime: `timeout_wrapper_exit=124` (expected), 10 repeated boots with `HELLCORE GUEST COLD RESTART REQUESTED` and `RESET_TYPE_REQUESTED=0x0000000000000000`.

Initial verifier exit was `1` because it defaulted to the warm marker. Corrected command:

```sh
env RESTART_VARIANT=COLD \
  RESTART_MARKER='HELLCORE GUEST COLD RESTART REQUESTED' \
  python3 "$W/tools/core20_restart_verify.py" \
    "$W/evidence/core20/qemu/oneshot-restart-cold/serial.log" \
    "$W/evidence/core20/qemu/oneshot-restart-cold/qemu-stderr.log" \
    "$W/evidence/core20/qemu/oneshot-restart-cold/TIMING.txt" \
    "$W/evidence/core20/qemu/oneshot-restart-cold/RESTART-VERIFICATION-COLD.txt"
```

Corrected exit: `0`; `repeated_init_and_restart_observed=true`.

### Interrupted initialization and recovery

Command:

```sh
env BUILD="$W/build/core20/kernel-oneshot-final1" \
  INTERRUPT_LABEL=oneshot-midinit RECOVERY_LABEL=oneshot-recovery \
  bash "$W/tools/core20_interrupted_midinit.sh"
```

Exit: `0`.
Interruption evidence: trigger `HELLCORE PMM: INVENTORY COMPLETE` seen `1`; `HELLCORE VMM: INITIALIZING` seen `1`; `HELLCORE KERNEL INITIALIZATION COMPLETE` seen `0`; shutdown marker seen `0`.
Fresh recovery boot: driver exit `0`, analyzer exit `0`, strict closure verifier exit `0`.

---

## 9. CR3 gate decision and evidence

Decision: **DEFERRED**. The gate was not bypassed.

Runtime evidence from PC and q35:

```text
CR3_CHANGED=0
CR3_ACTIVATION=DEFERRED
CR3_GATE_ROOT_OWNER=3
CR3_GATE_ROOT_CACHE=0
CR3_GATE_TABLE_COUNT=131
CR3_GATE_IDENTITY_FAILURES=0
CR3_GATE_PERMISSION_FAILURES=0
CR3_GATE_SUPERVISOR_FAILURES=0
CR3_GATE_WX_FAILURES=0
CR3_GATE_ALIAS_FAILURES=0
CR3_GATE_COVERAGE_FAILURES=0
CR3_GATE_CONTROLLED_TESTS_OK=1
CR3_GATE_RUNTIME_GATES_OK=0
CR3_GATE_ACTIVATION_AUTHORIZED=0
CR3_GATE_POST_SWITCH_TRANSLATIONS=MODEL_ONLY_NOT_RUNTIME
CR3_GATE_DECISION=DEFERRED_RUNTIME_GATES_UNPROVEN
```

Missing proofs: runtime post-switch translation check, exception delivery after switch, NMI/stack safety, validated recovery/fatal-stop after switch, and hardware-observed control-register semantics. See `reports/CORE20-ONESHOT-LIMITATIONS-AND-HARDWARE-VALIDATION.md`.

---

## 10. ECAM and cacheability status

- PC/i440fx: `MCFG_STATUS=ABSENT`, `MCFG_ENTRY_COUNT=0`, `MG_ECAM_RANGES_RESERVED=0`.
- q35: `MCFG_STATUS=OK`, `MCFG_TABLE_COUNT=1`, `MCFG_ENTRY_COUNT=1`, one entry for segment `0`, buses `0x00–0xff`, ECAM metadata base `0xE0000000`, aperture `0x10000000`, `ACCESS=DISCOVERED_NOT_MAPPED`.
- Both: `MCFG_ECAM_ADDRESSES_ARE_DISCOVERED_METADATA_NOT_MAPPED=1`, `MG_ECAM_ACCESS=METADATA_ONLY_NO_DEREFERENCE`.
- Both: `MG_PAT_SUPPORTED=1`, `MG_MTRR_SUPPORTED=1`, `MG_MTRR_VARIABLE_NON_WB=0`, synthetic MMIO cache `UC`, `MG_MMIO_FLAGS_OK=1`, `MG_MMIO_UNMAP_OK=1`, `MG_CACHE_FALLBACKS=0`, `MG_CACHEABILITY_REJECTS=0`, `MG_OWNERSHIP_REJECTS_FINAL=0`, `MG_ROLLBACK_COUNT=0`.

No ECAM dereference occurred. Bounds, bus range, alignment, overflow, ownership, alias, cacheability, and rollback behavior remain statically/model-covered only for ECAM metadata.

---

## 11. SMP status

SMP remains disabled. All runs used `-smp 1`. Runtime reports `SMP_ONLINE_CPUS_AFTER=1`, `SMP_AP_ENABLED=0`, and `SMP_GATE=DEFERRED_HARDWARE_PREREQUISITES_UNPROVEN`. No multi-vCPU test was run. AP startup, per-CPU state, synchronization, interrupt routing, and TLB shootdown are not implemented/validated.

---

## 12. Physical-hardware limitations

- No physical hardware validation.
- `/dev/kvm` absent; QEMU TCG only.
- No ECAM register access.
- No CR3 activation.
- No SMP activation.
- No external interrupt-controller hardware verification.
- Firmware, inline assembly, linker-provided symbols, and hardware side effects remain analysis boundaries.

---

## 13. Storage impact

| Item | Value |
|---|---|
| `/data` total | 105G |
| `/data` used | 104G |
| `/data` available at end | 544M |
| Full consolidated workspace | 88M |
| Each fresh kernel build output | ~675K |
| Four fresh build outputs total | ~2.7M |
| Campaign logs/output stayed inside existing workspace | yes |

No duplicate repository or duplicate build tree was created.

---

## 14. Preservation comparison

| Field | Before | After |
|---|---|---|
| Canonical HEAD | `a3858754bfc0bfa775f6288327dd4ff3da5142e4` | same |
| Canonical TREE | `956639cbab41d703941d982af7a8c86dcfcfcf2f` | same |
| Canonical staged entries | 0 | 0 |
| Canonical porcelain entries | 60 | 60 |
| Canonical porcelain SHA-256 | `3b4b9c1f6926ca16d13c8e31abeb765672f9321d5d4fa7adb1869351ac353a45` | same |
| Canonical modified tracked files | 0 | 0 |
| Canonical untracked files | 101 | 101 |
| Tested-source manifest check | 313/313 OK | 313/313 OK |
| Preservation targets checked | 30 | 30 |
| Changed preservation fields | 0 | 0 |

An initial preservation run reported 2 changed fields in the stage2-correction accepted workspace because of the empty `null.plist` described in section 3. After relocating only that analyzer artifact into the campaign logs, the final preservation run reported `targets_checked=30`, `changed_fields=0`.

Original consolidated patch, reports, test logs, preservation manifests, accepted checkpoints, canonical repository, and previously accepted workspaces were not overwritten.

---

## 15. Exact patch and SHA-256 manifests

| Deliverable | SHA-256 |
|---|---|
| `patches/CORE20-ONESHOT-CLOSURE.patch` | `20db79acc54e42e1248b72f66438705fb1213c222343bc0722b2f5b0627ca181` |
| `manifests/CORE20-ONESHOT-TESTED-SOURCE-SHA256SUMS.txt` | `0dfe8216f60c89305246dee00a31b9dcd30a4b94a4462912854b9edf07a30a4f` |
| Original tested-source manifest | `0dfe8216f60c89305246dee00a31b9dcd30a4b94a4462912854b9edf07a30a4f` |
| Original deliverables manifest | `86cda86dadd9dacb9d944bebfc179d795d21db2948763a5265153198826b9f01` |
| New one-shot deliverables manifest | `manifests/CORE20-ONESHOT-DELIVERABLES-SHA256SUMS.txt` |

The new one-shot deliverables manifest was generated after these reports and covers the one-shot reports, patch copy, tested-source manifest copy, and preservation report. Verify at any time with:

```sh
cd "$W" && sha256sum -c manifests/CORE20-ONESHOT-DELIVERABLES-SHA256SUMS.txt
```

---

## 16. Commands executed and exit codes

| Command | Exit |
|---|---:|
| `sha256sum -c CORE20-CONSOLIDATED-TESTED-SOURCE-SHA256SUMS.txt` | 0 |
| `sha256sum -c CORE20-CONSOLIDATED-DELIVERABLES-SHA256SUMS.txt` | 0 |
| `patch -p1 --dry-run --batch < CORE20-CONSOLIDATED-CLOSURE.patch` | 0 |
| Patch reconstruction comparison (`36/36` match) | 0 |
| `stage2_reproducible_build.sh` × 4 labels | 0 each |
| `core20_run_host_tests.sh oneshot-final1` | 0 |
| `core20_run_qemu.sh oneshot-final-pc 90` | 0 |
| `core20_run_qemu.sh oneshot-final-q35 90` | 0 |
| `core20_run_qemu_restart.sh oneshot-restart-warm 90` | 0 |
| `core20_run_qemu_restart.sh oneshot-restart-cold 90` | 1 (warm-default verifier marker only) |
| Cold verifier rerun with `RESTART_MARKER` | 0 |
| `core20_interrupted_midinit.sh` | 0 |
| Final source manifest recheck | 0 |
| First preservation check | 1 (external analyzer artifact) |
| Final preservation check after artifact relocation | 0 |

No packages were installed. No `Makefile clean`, build in canonical repository, source edit, fix, staging, commit, merge, cherry-pick, push, or publication occurred.

---

## 17. Remaining blockers

1. Physical hardware or KVM-backed validation.
2. Runtime CR3 activation gate: post-switch translation, exceptions, NMI/stack safety, and recovery/fatal-stop proof.
3. SMP hardware prerequisites and validation.
4. ECAM mapping/ownership/rollback proof and safe dereference target.
5. Hardware-observed cacheability and interrupt-controller behavior.

All blockers remain explicitly deferred. No safety gate was bypassed.

**Campaign status: complete for the existing tested revision. Recommended acceptance status: ACCEPT WITH LIMITATIONS.**

Stopping here; no remediation begun.
