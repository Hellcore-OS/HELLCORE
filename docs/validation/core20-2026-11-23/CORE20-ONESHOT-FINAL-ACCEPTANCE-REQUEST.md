# CORE2.0 one-shot campaign — final acceptance request

Independent reviewer entry point:

```text
reports/CORE20-ONESHOT-FINAL-IMPLEMENTATION-REPORT.md
```

Supporting evidence:

```text
reports/CORE20-ONESHOT-TESTED-REVISION.md
reports/CORE20-ONESHOT-LIMITATIONS-AND-HARDWARE-VALIDATION.md
reports/CORE20-ONESHOT-PRESERVATION.md
patches/CORE20-ONESHOT-CLOSURE.patch
manifests/CORE20-ONESHOT-TESTED-SOURCE-SHA256SUMS.txt
manifests/CORE20-ONESHOT-DELIVERABLES-SHA256SUMS.txt
build/core20/oneshot-20261123T222646Z/logs/
build/core20/host-tests/oneshot-final1/
evidence/core20/qemu/oneshot-final-pc/
evidence/core20/qemu/oneshot-final-q35/
evidence/core20/qemu/oneshot-restart-warm/
evidence/core20/qemu/oneshot-restart-cold/
evidence/core20/qemu/oneshot-midinit/
evidence/core20/qemu/oneshot-recovery/
```

## Requested decision

Accept the one-shot verification campaign as complete engineering evidence for the existing CORE2.0 tested revision, with the following explicit limitations retained:

- Runtime CR3 activation remains deferred.
- SMP remains disabled: single BSP only.
- ECAM remains metadata-only: no dereference.
- No physical-hardware or KVM validation was performed.
- Controlled exception/IRQ0 paths do not claim external interrupt-controller support.

Recommended status: `ACCEPT WITH LIMITATIONS`.

## What is being requested

1. Confirm the exact tested revision and preservation evidence.
2. Confirm host, build, PC, q35, restart, and recovery results as reproduced in this campaign.
3. Confirm that no source defect required a fix and that no unsafe gate was bypassed.
4. Preserve this package unchanged for the next authorized milestone: GitHub repository and README update after independent acceptance.

No merge, commit, stage, cherry-pick, push, or publication has been performed.
