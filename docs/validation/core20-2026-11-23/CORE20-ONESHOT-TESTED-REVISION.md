# CORE2.0 one-shot campaign — exact tested revision

Workspace: `/data/data/com.termux/files/usr/tmp/hellcore-consolidated-closure-20261123T210838Z`
Source root used for every fresh build and host suite: `$W/source`
Campaign log root: `$W/build/core20/oneshot-20261123T222646Z/logs`

## Revision identity

| Field | Value |
|---|---|
| Git HEAD | `a3858754bfc0bfa775f6288327dd4ff3da5142e4` |
| Git TREE | `956639cbab41d703941d982af7a8c86dcfcfcf2f` |
| Tested-source manifest entries | `313` |
| Tested-source manifest SHA-256 | `0dfe8216f60c89305246dee00a31b9dcd30a4b94a4462912854b9edf07a30a4f` |
| Consolidated patch SHA-256 | `20db79acc54e42e1248b72f66438705fb1213c222343bc0722b2f5b0627ca181` |
| Original deliverables manifest SHA-256 | `86cda86dadd9dacb9d944bebfc179d795d21db2948763a5265153198826b9f01` |
| Source staged entries | `0` |
| Source porcelain entries | `132` |

The isolated working source contains untracked local test/build helper artifacts, but the 313 canonical source inputs named by the manifest are the exact tested revision. No tracked source file was changed.

## Verification performed before the campaign

```sh
cd "$W/source" && sha256sum -c ../manifests/CORE20-CONSOLIDATED-TESTED-SOURCE-SHA256SUMS.txt
# rc=0; 313/313 OK

cd "$W" && sha256sum -c manifests/CORE20-CONSOLIDATED-DELIVERABLES-SHA256SUMS.txt
# rc=0; 31/31 OK

cd /data/data/com.termux/files/usr/tmp/hellcore-core20-final-impl-20261123T172926Z &&
  patch -p1 --dry-run --batch < "$W/patches/CORE20-CONSOLIDATED-CLOSURE.patch"
# rc=0
```

The 36-path consolidated patch was verified against the historical patch-apply reconstruction: `changed_paths=36`, `match=36`, `missing=0`, `mismatch=0`.

## Verification performed after the campaign

```sh
cd "$W/source" && sha256sum -c ../manifests/CORE20-CONSOLIDATED-TESTED-SOURCE-SHA256SUMS.txt
# rc=0; 313/313 OK
```

Git HEAD, TREE, staged entries, and canonical repository state were unchanged. The one-shot patch and tested-source manifest are byte-identical copies under new names:

- `patches/CORE20-ONESHOT-CLOSURE.patch`
- `manifests/CORE20-ONESHOT-TESTED-SOURCE-SHA256SUMS.txt`

## Historical evidence linkage

The reported baseline — 15 host suites, 1,894 `PASS:` assertions, four builds with 107 validation checks each, PC/q35 QEMU runs, controlled exception/IRQ0 verification, warm/cold restart, and interrupted-initialization recovery — is linked to this exact tested-source manifest and patch. This campaign reproduced those results from fresh builds using the same tested source; it did not introduce a new source revision.

## Conclusion

The current tested source matches the reported tested revision exactly. No source differences were found. No source changes were made in this campaign.
