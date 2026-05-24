<!--
Copyright 2026 Paolo Anzani

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# AGENTS instructions

Guidance for AI coding agents working in this repository. See
<https://agents.md/> for the file convention.

## Naming

- Write the project/tool name as `jail` in prose and commands.
- Write `rootfs` for generated chroot trees.
- Preserve literal Apple/XNU/dyld names exactly: `dyld`, `AMFI`, `SIP`,
  `Mach-O`, `CodeDirectory`, `SuperBlob`, `CSSLOT_SIGNATURESLOT`,
  `CSMAGIC_BLOBWRAPPER`, `CS_VALID`, `LC_CODE_SIGNATURE`.
- Preserve constants exactly: `CPU_SUBTYPE_ARM64E_V0`,
  `CPU_SUBTYPE_ARM64E_V1`, `DYLD_SHARED_CACHE_DIR`,
  `DYLD_SHARED_REGION=private`.

## Environment Setup

- Target platform: Apple Silicon macOS 14+.
- Required tools: Xcode Command Line Tools (`clang` or `cc`, `codesign`,
  `otool` when investigating Mach-O state), POSIX `sh`, `sudo`, and `gktool`
  (stock since macOS 14, used by `bootstrap` to pre-warm Gatekeeper on the
  copied dyld shared cache).
- Do not require third-party dependencies. In particular, do not introduce
  `ldid` or any non-stock macOS tool for normal build/test paths.
- Do not require SIP disabling, boot-arg changes, kernel-extension changes, or
  other host security reconfiguration.
- Most runtime commands require root because `chroot(2)` and dyld cache copying
  require it.
- `rootfs/` is generated, gitignored, and usually root-owned after bootstrap.
  Clean it with `sudo rm -rf rootfs`.

## Commands

- Build the project: `make build`
- Build directly: `cc -o jail jail.c`
- Run full test suite: `sudo sh tests/run.sh`
- Run one test file: `sudo sh tests/run.sh tests/006-system-binary.sh`
- Keep failing test workdir for inspection: `JAIL_TEST_KEEP=1 sudo sh tests/run.sh`
- Find agent TODOs when asked: `rg -F 'TODO (AGENT)'`

Usage commands:

```sh
sudo ./jail bootstrap <rootfs>
sudo ./jail install   <rootfs> <host-path> <rootfs-path>
sudo ./jail run       <rootfs> <binpath> [args...]
sudo ./jail shell     <rootfs> <shellpath> [args...]
```

## Repository Structure

- `jail.c`: entire tool, single C translation unit.
- `Makefile`: build/install/uninstall/clean targets.
- `rootfs/`: generated runtime tree, gitignored, root-owned after bootstrap.
- `tests/run.sh`: sh-only test runner. Builds `jail`, prepares a temporary
  rootfs, generates shell fixture scripts, sources numbered tests.
- `tests/[0-9][0-9][0-9]-*.sh`: deterministic stdout tests.
- `tests/TESTS.md`: test format and authoring rules.

## Architecture Boundaries

1. `bootstrap` creates the minimal dyld-compatible rootfs layout.
2. `bootstrap` copies `/usr/lib/dyld` to `<rootfs>/usr/lib/dyld`.
3. `bootstrap` copies dyld shared cache files from
   `/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e*`
   to the same path inside the rootfs.
4. `bootstrap` pre-warms Gatekeeper on every copied
   `dyld_shared_cache_arm64e` and `dyld_shared_cache_arm64e.NN` slice via
   `gktool scan`. See "First-Run Gatekeeper Scan" for the motivation.
5. `install` copies a host binary into the rootfs, patches Apple Silicon arm64e
   ABI metadata when required, ad-hoc re-signs patched files, hides the empty
   CMS slot left by `codesign`, and chmods the result executable.
6. `run` forks, redirects child stdout/stderr to a pipe, chroots, `chdir("/")`,
   and executes the requested binary.
7. `run` must pass the dyld shared-cache environment:
   `DYLD_SHARED_CACHE_DIR=<CACHE_DIR>` and `DYLD_SHARED_REGION=private`.
8. `shell` uses `forkpty(3)` so the child has a terminal-like fd set, then
   chroots and execs the requested shell path.

## Security Model

- `jail` is not a production security boundary.
- `chroot(2)` changes filesystem name resolution (`fd_rdir`) only.
- Open file descriptors survive chroot and can expose host resources.
- The current working directory must be moved into the new root before chroot
  and reset to `/` after chroot to avoid classic cwd-based escapes.

## Non-Negotiable Invariants

- Do not remove `DYLD_SHARED_CACHE_DIR` or `DYLD_SHARED_REGION=private` from
  `cmd_run` or equivalent exec paths. Without them dyld tries XNU's shared-region
  cache mapping path, expects a SIP-protected cache vnode, and fails with
  `syscall to map cache into shared region failed`.
- Do not simplify the install pipeline by removing `patch_arm64e_abi_v1()`,
  `resign_adhoc()`, or `hide_cms_slot()`.
- Preserve install order for patched arm64e system binaries:

```text
cp -f -> patch_arm64e_abi_v1 -> resign_adhoc -> hide_cms_slot -> chmod
```

- Do not move `hide_cms_slot()` before `resign_adhoc()`. `codesign` rewrites the
  SuperBlob and would undo the slot rewrite.
- Do not remove `prewarm_gatekeeper()` from `cmd_bootstrap` as a simplification.
  Without it the first `./jail run` against a fresh rootfs blocks for ~30 s
  while `syspolicyd` scans the copied dyld shared cache. See "First-Run
  Gatekeeper Scan".
- Do not patch only one arm64e cpusubtype location. XNU cross-checks the fat
  arch table and inner Mach-O header and returns `EBADARCH` on mismatch.
- Do not treat stderr from the child as authoritative for exec/load failures.
  Kernel-side AMFI/code-signing failures often only appear in system logs.
- Keep the large explanatory comments in `jail.c` load-bearing. Update them when
  behavior changes; do not delete them to make code look shorter.

## Apple Silicon Hardened ABI Model

Stock Apple platform binaries on the SSV, such as `/bin/sh`, `/bin/ls`, and
`/usr/bin/dig`, are arm64e with ptrauth ABI version 0:
`cpusubtype = CPU_SUBTYPE_ARM64E_V0 = 0x80000002`. The high bit means
"versioned ABI" and the next nibble is the version number. They run on the live
system only because they are Apple platform binaries.

Two gates matter inside a chroot:

1. dyld strips `DYLD_*` environment variables from Apple platform binaries.
   `opts.forcePrivate` is only set when AMFI's
   `security.allowEnvVarsSharedCache` is true and `DYLD_SHARED_REGION=private`
   is present. For Apple-signed platform binaries AMFI says no, so the env vars
   are silently dropped. Result: the private shared-cache workaround has no
   effect on an unmodified `/bin/sh`.
2. XNU's `exec_mach_imgact` rejects non-platform arm64e v0. The check is
   effectively: if `cpusubtype == CPU_SUBTYPE_ARM64E`, ptrauth ABI version is
   `0`, the binary is not a platform binary, and `-arm64e_preview_abi` is not
   enabled, refuse exec. Removing the Apple signature fixes gate 1 but exposes
   this gate.

Consequences:

- Resign alone: DYLD env vars are honored, but the kernel rejects de-platformed
  arm64e v0.
- Cpusubtype patch alone: DYLD env vars are still stripped because the Apple
  signature/platform status remains, and patched bytes no longer match the
  original CDHash.
- Patch plus ad-hoc resign, in that order: cpusubtype becomes v1
  (`CPU_SUBTYPE_ARM64E_V1 = 0x81000002`), the patched bytes are covered by a
  fresh ad-hoc signature, platform status is removed, DYLD env vars are honored,
  and the private shared-cache mapping succeeds.

`patch_arm64e_abi_v1()` patches two fields:

1. Fat arch table entry, big-endian, at file offset
   `8 + i * arch_table_size + 4`.
2. Inner Mach-O header cpusubtype, little-endian, at slice offset `8`.

After bytes change, `resign_adhoc()` must remove the stale signature and run
`codesign --force --sign -` so the file is executable on Apple Silicon.

## CMS-Slot Trap

Symptom: after `install`, a system binary runs once, then subsequent
`./jail run` executions of the same file exit 1 silently. The file on disk is
byte-identical across runs: same inode, sha256, mtime, and xattrs. Kernel state
has changed.

Typical log output:

```text
load_code_signature: embedded signature doesn't match attached signature
proc N: load code signature error 2 for file "<name>"
```

Cause:

- `codesign --force --sign -` emits a SuperBlob containing an empty
  `CSSLOT_SIGNATURESLOT` (`type = 0x10000`) `CSMAGIC_BLOBWRAPPER`
  (`magic = 0xfade0b01`) entry.
- The entry is just an 8-byte wrapper with no real CMS data.
- AMFI's exec-time logic in xnu `bsd/kern/kern_exec.c` treats a binary as a
  simple ad-hoc signature only when this lookup returns NULL:

```c
csblob_find_blob_bytes(SUPERBLOB, CSSLOT_SIGNATURESLOT, CSMAGIC_BLOBWRAPPER) == NULL
```

- The empty wrapper makes the lookup non-NULL, so AMFI rejects the "simple
  adhoc" classification.
- Closed Apple policy code can then call `csvnode_invalidate_flags` on the
  vnode, clearing `CS_VALID` on the cached `cs_blob`.
- On the next exec, `load_code_signature` in `bsd/kern/mach_loader.c` finds the
  cached invalid blob via `ubc_cs_blob_get` and returns `LOAD_BADMACHO`.
- The file is not mismatched. The per-vnode kernel cache is poisoned.

Implemented fix:

- `hide_cms_slot()` runs after `resign_adhoc()`.
- It rewrites only the SuperBlob index entry `type` for `CSSLOT_SIGNATURESLOT`
  from `0x10000` to `CSSLOT_HIDDEN_CMS = 0xfffe`.
- It does not touch CodeDirectory bytes, so the CDHash remains valid.
- AMFI's lookup misses, the binary qualifies as simple ad-hoc, and `CS_VALID`
  is not invalidated after first exec.

Dead ends already tried. Do not retry unless the user explicitly asks for
historical validation:

- `chflags schg` or `chflags restricted` on resigned binaries: AMFI still
  invalidates the blob.
- `cp -f` over the existing path before each run: preserves inode/vnode and the
  poisoned `cs_blob`.
- `touch rootfs/<bin>` between runs: does not invalidate the cache.
- `MS_SYNC` vs `MS_ASYNC` in the patch path: no behavioral difference.
- Removing `--remove-signature` from `resign_adhoc`: `codesign` still emits the
  empty CMS slot.
- Removing `com.apple.provenance`: kernel re-adds it; not relevant.
- Running the binary outside the chroot to see what fails: it SIGKILLs because
  there is no compatible dyld cache; the chroot is required for meaningful
  reproduction.
- Using `ldid`: not installed on stock macOS and not an accepted dependency.

## First-Run Gatekeeper Scan

Symptom: the first `./jail run` against a freshly bootstrapped rootfs blocks
for ~30 s before producing any output, regardless of which binary is
invoked. Subsequent runs are sub-second. `user` and `sys` time inside the
jail process are both ~0; the latency is wall-clock wait on another daemon.

Cause:

- `bootstrap` `cp`s the dyld shared cache out of
  `/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/` (volume
  `disk3s2`) into `<rootfs>/...` (volume `disk3s5`). The volumes share an
  APFS container but `clonefile(2)` cannot operate cross-volume, so the
  ~4.6 GB cache is physically copied.
- The kernel auto-tags the destination files with `com.apple.provenance`
  (the SIP-protected originals have no such xattr) and the copies are not
  covered by any trustcache entry.
- When dyld inside the chroot mmaps a slice as executable, AMFI escalates
  to `syspolicyd`, which runs the full Gatekeeper / XProtect pipeline:
  `signatures didn't match -> performing resource validation ->
  GK Xprotect results / XPScan -> GK evaluateScanResult -> YARA scan
  threshold`. Each ~2 GB slice takes ~15 s on M-series hardware.
- After the scan, syspolicyd caches the evaluation by content hash (PST),
  so subsequent execs short-circuit. The cache survives reboots and is
  NOT cleared by `sudo purge` (which only drops UBC pages, not the GK
  database).

Implemented fix:

- `prewarm_gatekeeper()` runs at the end of `cmd_bootstrap`, after the
  `cp -f` of the cache slices and before installing `DEFAULT_TOOLS`.
- It invokes `gktool scan <slice>` on every
  `dyld_shared_cache_arm64e` and `dyld_shared_cache_arm64e.NN` file in
  the rootfs. Apple documents this CLI as: "Performs a Gatekeeper scan
  of the specified path ... useful for pre-warming the cache so users
  do not see the 'Verifying...' dialog on first launch."
- The total scan cost (~30 s for two slices on M-series) is identical
  to what the user used to pay on the first run; it is now folded into
  `bootstrap`, where the user is already waiting on the multi-GB copy.
- Only the executable-mapped slices are scanned. `.atlas` and `.map`
  are metadata files that dyld does not exec-map and do not trigger the
  AMFI/syspolicyd path.

Dead ends already tried. Do not retry unless the user explicitly asks for
historical validation:

- Reading the cache files with `cat` / `dd` before the first run: warms
  the UBC but does not trigger executable-map policy, so the GK scan
  still runs at first exec.
- `sudo purge`: drops UBC pages but leaves syspolicyd's evaluation cache
  intact; does not reproduce the slowness on an already-warmed rootfs.
- Stripping `com.apple.provenance` post-copy: the kernel re-adds it
  before the next `stat`, and the GK scan still fires (see also the
  CMS-Slot Trap dead-end list).
- Running two `gktool scan` invocations in parallel: `syspolicyd`
  serializes evaluations internally, so wall time is unchanged.
- `spctl --master-disable`: requires SIP off and weakens the host
  security posture; not acceptable per "Environment Setup".
- Avoiding the cross-volume copy: the cache lives on the SIP-protected
  Preboot volume; there is no stock-macOS path that lets the rootfs
  reference it without the copy.

## Testing Standards

- Tests are sh-only. Do not add Python, Ruby, Node, or compiled fixture sources
  to the committed test harness.
- Test format is documented in `tests/TESTS.md`.
- Tests should use `expect_stdout` and deterministic stdout heredocs.
- Avoid asserting PIDs, timestamps, inode numbers, mtime, random bytes, dyld
  UUIDs, raw kernel log counts, or machine-local output unless normalized.
- For nonzero exit behavior, print `$?` as part of expected stdout and make the
  final shell command exit 0.
- Do not pipe `./jail run` when measuring its exit code. A pipeline reports the
  last command's exit code in common shells and can hide the real failure.
- Always test installed Apple system binaries in series of three bare runs when
  touching install, patching, signing, or cache-related code.
- `tests/006-system-binary.sh` is the regression shield for the CMS-slot trap.
  Keep it strict.

Canonical three-run manual check:

```sh
for i in 1 2 3; do
  echo "run $i:"
  sudo ./jail run ./rootfs /bin/ls
  echo "  rc=$?"
done
```

If output filtering is necessary, redirect stdout to a file first and inspect it
after recording the command's status:

```sh
sudo ./jail run ./rootfs /usr/bin/dig -h > /tmp/out.txt
echo "rc=$?"
head -3 /tmp/out.txt
```

## Debugging Playbook

Use this order for dyld/AMFI/code-signing/chroot bugs.

1. Trust system logs, not child stderr. `cmd_run` captures stderr in a pipe, but
   exec/load failures can happen after `execve` succeeds and before user code
   prints anything.

```sh
sudo log stream --info --debug --style ndjson \
  --predicate '(eventMessage CONTAINS[c] "signature"
              OR eventMessage CONTAINS[c] "AMFI"
              OR eventMessage CONTAINS[c] "AppleSystemPolicy"
              OR eventMessage CONTAINS   "load_code"
              OR eventMessage CONTAINS   "cs_blob"
              OR eventMessage CONTAINS[c] "vnode"
              OR eventMessage CONTAINS   "provenance")' \
  > /tmp/jaillog.ndjson &
LOGPID=$!
sleep 1
# reproduce failure here
sleep 1
sudo kill $LOGPID
```

Search `/tmp/jaillog.ndjson` for the binary name and messages containing
`AMFI`, `load_code_signature`, `Invalidated flags`, `Possible race detected`,
`ignoring detached code signature`, `cs_blob`, or `ASP`.

2. Take the exact kernel error string to xnu source.

```sh
curl -sL https://github.com/apple-oss-distributions/xnu/archive/refs/heads/main.tar.gz \
  -o /tmp/xnu.tar.gz
tar -C /tmp -xzf /tmp/xnu.tar.gz
grep -rn "embedded signature doesn't match" /tmp/xnu-main/
```

Cross-reference `osfmk/kern/cs_blobs.h` for `CS_*` flags and slot constants.
Remember that AMFI and AppleSystemPolicy policy code is not open-source; if a
caller is missing from xnu, defeat the condition being tested rather than trying
to inspect the closed caller.

3. Prove kernel state vs file state before mutating code.

```sh
stat -f "inode=%i mtime=%m size=%z" rootfs/bin/ls
shasum -a 256 rootfs/bin/ls
xattr -l rootfs/bin/ls
```

Capture before run 1, after run 1, and after run 2. If file facts are identical
but behavior changes, suspect kernel caches: `cs_blob`, UBC, AMFI trustcache.
To prove per-vnode poisoning, remove the file and reinstall so the path gets a
fresh inode: `sudo rm rootfs/<path> && sudo ./jail install ...`.

4. Do not trust `codesign -dvvv` summaries. Parse the SuperBlob manually when
   investigating CMS-slot behavior.

```python
import struct
path = "rootfs/bin/ls"
with open(path, "rb") as f:
    data = f.read()

fat_magic, nfat = struct.unpack(">II", data[:8])
slice_off = None
for i in range(nfat):
    cputype, _, off, _, _ = struct.unpack(">IIIII", data[8+i*20:8+i*20+20])
    if cputype == 0x0100000c:
        slice_off = off

ncmds = struct.unpack("<I", data[slice_off+16:slice_off+20])[0]
cmd_off = slice_off + 32
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack("<II", data[cmd_off:cmd_off+8])
    if cmd == 0x1d:
        dataoff = struct.unpack("<I", data[cmd_off+8:cmd_off+12])[0]
        sb_off = slice_off + dataoff
        sb_magic, sb_len, count = struct.unpack(">III", data[sb_off:sb_off+12])
        print(f"SuperBlob magic=0x{sb_magic:x} len={sb_len} count={count}")
        for i in range(count):
            typ, off = struct.unpack(">II", data[sb_off+12+i*8:sb_off+12+i*8+8])
            m, l = struct.unpack(">II", data[sb_off+off:sb_off+off+8])
            print(f"  slot[{i}] type=0x{typ:x} magic=0x{m:x} length={l}")
        break
    cmd_off += cmdsize
```

Recognize these slot types:

- `0x0`: CodeDirectory, `CSMAGIC_CODEDIRECTORY = 0xfade0c02`.
- `0x2`: Requirements, `0xfade0c01`.
- `0x10000`: `CSSLOT_SIGNATURESLOT`, CMS blob wrapper, `0xfade0b01`.
- `0xfffe`: hidden marker set by `hide_cms_slot()`.

If `0x10000` has `length=8`, the CMS-slot trap is active. After
`hide_cms_slot()` it should appear as `0xfffe`.

5. Test binary field mutations in Python before porting them to C. Use this for
   one-bit or one-field hypotheses, then port the proven byte-level logic to a
   static function in `jail.c` and wire it into `cmd_install`.

```sh
sudo ./jail install ./rootfs <host> <inside>
sudo python3 <<'EOF'
# mutate one field here
EOF
# run the binary three times with bare exit codes
```

6. Ignore `/bin` vs `/usr/bin` folk taxonomies until clean tests prove a real
   path-specific difference. XNU cares about signature shape, cpusubtype, vnode
   state, and post-resign SuperBlob structure, not the SSV directory name.

## Working With rootfs

- Treat rootfs directories as ephemeral generated artifacts.
- `bootstrap` copies multi-GB dyld shared cache files and then runs
  `gktool scan` over each slice to pre-warm Gatekeeper. Bootstrap is
  therefore slow (tens of seconds on M-series); subsequent `install` and
  `run` invocations against the same rootfs are fast.
- Once a binary is installed and executed, XNU may cache a `cs_blob` for its
  vnode. If re-testing patch/signing logic, remove the installed file first so
  reinstall creates a fresh vnode.
- Do not use `cp -f` over an already-executed installed binary as a cache-reset
  mechanism. It preserves the inode.

## Coding Standards

- Prefer minimal correct C changes in `jail.c`. Keep the single-translation-unit
  structure unless the user explicitly asks for a refactor.
- Avoid new dependencies and new helper binaries.
- Keep comments that encode causal models for dyld, AMFI, SIP, XNU, Mach-O, or
  code signing. Update comments with behavior changes.
- Preserve direct `execve`-based behavior when modifying run paths; do not route
  through `system(3)` for child execution.
- Preserve `chdir(rootfs)`, `chroot(".")`, then `chdir("/")` semantics unless a
  change explicitly proves an equivalent or safer sequence.
- Be cautious with file replacement under `rootfs` after execution because vnode
  identity matters.

## Commits

- Use this commit message format when the user explicitly asks for a commit:
  `(<commit type>) (<ai model>, <human reviewed T|F>, <tested T|F>) <commit text>`.
- Examples: `(feat) (GPT-5.5, F, T) add new logging system`,
  `(fix) (GPT-5.5, T, T) preserve dyld cache environment`.
- Before creating a commit, ask the user whether a human reviewed the changes so
  the second metadata field can be set to `T` or `F` accurately.
- Set the tested field to `T` only when relevant tests or verification commands
  were run successfully for the committed changes. Otherwise set it to `F`.
- Keep commit text concise and focused on user-visible impact or the regression
  shield being added.

## Boundaries

- Ask first before broad refactors, new dependencies, behavior changes that
  weaken the documented dyld/cache/signing model, or changes requiring host
  security reconfiguration.
- Never remove the dyld env vars, arm64e patching, ad-hoc resigning, or CMS-slot
  hiding as a simplification.
- Never use destructive git operations unless explicitly requested.
- Never treat this tool as a production security boundary in docs or code.

## References

- XNU source: <https://github.com/apple-oss-distributions/xnu>
- `bsd/kern/mach_loader.c::load_code_signature`: embedded signature mismatch
  site.
- `bsd/kern/kern_cs.c::csblob_invalidate_flags`: clears `CS_VALID`.
- `bsd/kern/kern_exec.c`: AMFI simple-ad-hoc gate; search for
  `ignoring detached code signature`.
- `osfmk/kern/cs_blobs.h`: `CS_*` flag and slot constants.
- Apple newosxbook, "Code Signing - Hashed Out":
  <https://www.newosxbook.com/articles/CodeSigning.pdf>
