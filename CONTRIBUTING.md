# Contributing to vCUDA-kernel

Thanks for helping improve `vCUDA-kernel`. This project is a Linux kernel module
for NVIDIA GPU resource tracing, accounting, and enforcement research. Changes
should stay small, auditable, and easy to validate on real GPU hosts.

## Development Principles

- Keep kernel changes minimal and boring.
- Prefer existing Linux kernel APIs and existing local helpers.
- Keep UAPI changes explicit, versionable, and documented.
- Default new enforcement behavior to dry-run until verified.
- Do not mix unrelated refactors with functional changes.
- Do not vendor new dependencies unless there is no practical alternative.

## Repository Layout

```text
core/        core policy, task, stats, cgroup, and accounting logic
ctl/         /dev/vgpuctl, debugfs, and event trace surfaces
nvidia/      NVIDIA device hook, RM ioctl parsing, and enforcement paths
include/     public UAPI headers
examples/    CUDA and vgpuctl verification helpers
scripts/     host verification scripts
tests/       KUnit and userspace tests
docs/        design and flow documentation
```

## Build

On a Linux GPU host with matching kernel headers:

```bash
git submodule update --init --recursive
make clean
make
make example
```

Optional CMake wrapper:

```bash
cmake -S . -B build
cmake --build build
```

## Verification

Run the narrowest checks that cover your change:

```bash
make load
make fingerprint
make verify-cgroup
make verify-cgroup-policy
make verify-cgroup-compute
make verify-cgroup-memory
make verify-compute
make test-kunit
make unload
```

Verification scripts print `PASS:` on success and `FAILED:` on failure. Include
the relevant output in issue reports and pull requests.

## Coding Rules

- Use SPDX headers in new source files.
- Keep UAPI headers under `include/` compatible with C userspace.
- Do not break existing `struct` size checks in `core/vgpu_main.c` without
  explaining the UAPI impact.
- Avoid large stack allocations in kernel code.
- Use kernel allocation and locking primitives consistently with nearby code.
- Keep debugfs read-only for diagnostics; use `/dev/vgpuctl` for control.

## Submodules and Licensing

`third_party/open-gpu-kernel-modules` is a reference submodule for NVIDIA OKM
layout and ioctl research. It is not built into `vgpu-kernel.ko`.

Do not copy incompatible code into this repository. New kernel source files
should use:

```text
SPDX-License-Identifier: GPL-2.0-only
```

UAPI headers should use:

```text
SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note
```

## Pull Request Checklist

- Build passes on a matching Linux kernel header host.
- Relevant verification target prints `PASS:`.
- KUnit coverage is added or updated for shared logic.
- README/docs are updated for UAPI or behavior changes.
- No unrelated generated files, build outputs, or local logs are included.
- New behavior is dry-run first unless enforcement was explicitly requested.

## Reporting Issues

Include:

- kernel version: `uname -r`
- NVIDIA driver version
- OKM/GSP status from `make fingerprint`
- module load command
- failing command and `FAILED:` output
- relevant debugfs output from `/sys/kernel/debug/vgpu/*`

## Conduct

All participation is governed by [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
