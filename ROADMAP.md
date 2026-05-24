# Roadmap

`jail` is early-stage. The roadmap is intentionally lightweight: it explains
project direction, not the live task list.

Roadmap intent belongs here.

The roadmap may change as the implementation, macOS behavior, and user needs
become clearer.

## Use case

One reason `jail` exists is to explore a no-SIP-disabling macOS command sandbox
for AI agents and build tools. The goal is a lighter native Apple environment
than Linux VM-based containers: commands run against macOS userspace, dyld, and
Apple Silicon behavior similar to the host they are actually running on, while
still getting a constrained filesystem view and future layers such as privilege
dropping, resource limits, and sandbox profiles.

This is not meant to claim Linux-container-equivalent isolation. The useful
target is a pragmatic macOS-native boundary for development, testing, and agent
training workflows where a full VM is too heavy but unconstrained host execution
is not acceptable.

## Current focus

- Stabilize the core `bootstrap`, `install`, `run`, and `shell` workflow.
- Build regression coverage around dyld cache setup, arm64e patching,
  ad-hoc signing, CMS-slot hiding, chroot execution, and pty behavior.
- Improve command-line ergonomics, especially subcommand arguments and flags.
- Document the macOS internals encountered while building the tool: dyld,
  Mach-O loading, AMFI, SIP, code signing, chroot, ptys, and process behavior.
- Keep the project usable without disabling SIP and without third-party runtime
  dependencies.

## Planned

- Dropping privileges in the child after chroot, so the runner can require root
  while the jailed process does not remain root.
- Resource limits through stable public APIs such as `setrlimit(2)`.
- Optional macOS sandbox profiles via `sandbox_init(3)` to complement the
  filesystem view restriction with syscall-level policy.
- Bind-style host directory sharing where macOS allows a practical and
  well-documented implementation.
- Pre-built rootfs artifacts or a simple image format for repeatable setup.
- Better examples that show realistic shells and installed tools without
  overstating the security model.
- OCI-compatible rootfs packaging, so a rootfs can be represented as an image
  artifact and potentially moved through existing registry tooling.
- Layered or overlay-style rootfs workflows, if they can be implemented cleanly
  on macOS without weakening the model.
- A process-namespace illusion for read-only process views, likely through
  filtered process listing rather than real PID isolation.
- x86_64 support, if the dyld cache and signing model can be supported without
  complicating the Apple Silicon path.
- Performance improvements for repeated rootfs creation, such as APFS cloning or
  cached installed binaries.

## How to contribute

Before opening a large PR, open an issue or discussion first so the scope can be
aligned with the project model.

- Use GitHub Issues for concrete bugs, features, and research tasks.
- Keep implementation PRs paired with deterministic tests when behavior changes.
- When proposing macOS internals work, include references or reproduction notes
  so the behavior can be verified.
