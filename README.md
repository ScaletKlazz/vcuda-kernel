# vCUDA-kernel

[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel.svg?type=shield&issueType=license)](https://app.fossa.com/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel?ref=badge_shield&issueType=license)
![CodeQL](https://github.com/ScaletKlazz/vcuda-kernel/actions/workflows/codeQL.yml/badge.svg)
![Build](https://github.com/ScaletKlazz/vcuda-kernel/actions/workflows/kernel-build.yml/badge.svg)
![Issues](https://img.shields.io/github/issues/ScaletKlazz/vcuda-kernel)
![Release](https://img.shields.io/github/v/release/ScaletKlazz/vcuda-kernel?display_name=tag)
![License](https://img.shields.io/github/license/ScaletKlazz/vcuda-kernel)

`vCUDA-kernel` is the Linux kernel enforcement component of the vCUDA project.
It provides a kernel-side control and tracing layer for NVIDIA GPU resource
virtualization experiments, with the long-term goal of enforcing GPU memory and
compute scheduling policy without relying on `LD_PRELOAD`.

The repository is intended to be published separately from the user-space
`vcuda-core` runtime and the Kubernetes `device-plugin` integration.

## Features

- Linux kernel module: `vgpu-kernel.ko`.
- Control device: `/dev/vgpuctl`.
- Debugfs diagnostics under `/sys/kernel/debug/vgpu`.
- NVIDIA driver fingerprint detection at module load.
- NVIDIA character-device open, ioctl, and release tracing.
- Per-task GPU context tracking.
- GPU memory accounting core.
- Dry-run-first safety model for future enforcement paths.

## Prerequisites

- Linux host with matching kernel headers installed.
- NVIDIA 570.x driver target environment.
- NVIDIA Open Kernel Modules recommended.
- GSP firmware enabled for the primary development path.
- `make` and `gcc`.
- `cmake` optional.
- CUDA toolkit optional, only needed for examples.
- root privileges for module load and unload.

Ubuntu example:

```bash
sudo apt update
sudo apt install build-essential cmake linux-headers-$(uname -r)
```

If kernel module build fails with unresolved `module_layout`, check that the
matching kernel header package is installed and that
`/lib/modules/$(uname -r)/build/Module.symvers` is not empty.

## Quick Start

```bash
git submodule update --init --recursive
make clean
make
make load
make fingerprint
cat /sys/kernel/debug/vgpu/hooks
cat /sys/kernel/debug/vgpu/stats
make unload
```

`make load` defaults to dry-run mode and passes detected NVIDIA driver metadata
to the module. It also enables conservative local GPU-memory accounting with
NVIDIA 570 OKM layout defaults. Override with make variables when needed:

```bash
make load MEMORY_TRACE=0
make load MEMORY_ALLOC_MIN_BYTES=$((64 * 1024 * 1024))
```

## Build

Build with Kbuild through the project `Makefile`:

```bash
make
```

Equivalent explicit command:

```bash
make -C /lib/modules/$(uname -r)/build M=$PWD modules
```

Clean build outputs:

```bash
make clean
```

Build examples:

```bash
make example
```

Clean examples:

```bash
make example-clean
```

### CMake Wrapper

CMake wraps the existing Kbuild flow. It does not replace Kbuild.

```bash
cmake -S . -B build
cmake --build build
```

Useful targets:

```bash
cmake --build build --target load
cmake --build build --target unload
cmake --build build --target reload
cmake --build build --target fingerprint
cmake --build build --target debug-hooks
cmake --build build --target debug-tasks
cmake --build build --target debug-stats
cmake --build build --target debug-events
```

## Load And Unload

Load in default dry-run mode:

```bash
make load
```

Useful load variables:

```text
DRY_RUN=1
ALLOW_ENFORCE=0
MEMORY_TRACE=1
MEMORY_ALLOC_MIN_BYTES=16777216
MEMORY_ALLOC_MAX_BYTES=0
MEMORY_TRACE_LIMIT_BYTES=0
CLEAR_MEMORY_ON_LAST_CLOSE=1
```

Unload:

```bash
make unload
```

Reload:

```bash
make reload
```

The first enforcement-capable path requires all of the following:

- supported NVIDIA driver fingerprint;
- explicit policy;
- `dry_run=0`;
- `allow_enforce=1`.

By default, the module does not deny allocations and does not rewrite NVIDIA
scheduling state.

## Test

Run a basic module validation:

```bash
make clean
make
make load
make fingerprint
nvidia-smi >/dev/null
cat /sys/kernel/debug/vgpu/stats
cat /sys/kernel/debug/vgpu/events | tail -n 30
make unload
```

Expected high-level result:

- module loads without `Oops`, `BUG`, or hook registration failure;
- driver fingerprint is visible;
- NVIDIA ioctl counters increase after `nvidia-smi`;
- unload completes cleanly.

Build and run CUDA example workload:

```bash
make example
./examples/cuda_malloc_smoke $((256 * 1024 * 1024)) 4 0
```

## Debugfs

Mount debugfs if needed:

```bash
sudo mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
```

Common diagnostic files:

```text
/sys/kernel/debug/vgpu/enabled
/sys/kernel/debug/vgpu/driver_fingerprint
/sys/kernel/debug/vgpu/hooks
/sys/kernel/debug/vgpu/policies
/sys/kernel/debug/vgpu/tasks
/sys/kernel/debug/vgpu/events
/sys/kernel/debug/vgpu/ioctls
/sys/kernel/debug/vgpu/stats
```

Quick checks:

```bash
cat /sys/kernel/debug/vgpu/hooks
cat /sys/kernel/debug/vgpu/tasks
cat /sys/kernel/debug/vgpu/ioctls
cat /sys/kernel/debug/vgpu/stats
cat /sys/kernel/debug/vgpu/events | tail -n 80
```

## NVIDIA OKM Reference

This repository includes NVIDIA Open GPU Kernel Modules as a pinned reference
submodule under `third_party/open-gpu-kernel-modules`.

The submodule is used for ABI/layout lookup and RM ioctl structure references.
It is not built into `vgpu-kernel.ko`.

Initialize it with:

```bash
git submodule update --init --recursive
```

## Repository Split

Recommended public repositories:

- `vCUDA-kernel`: kernel-side enforcement and tracing.
- `vCUDA-device-plugin`: Kubernetes device-plugin, CDI, and cgroup policy injection.
- `vCUDA-core`: user-space CUDA virtualization and remote call transport.

## License

[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel.svg?type=large&issueType=license)](https://app.fossa.com/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel?ref=badge_large&issueType=license)

GPL-2.0-only. Source files use SPDX license identifiers. UAPI headers use
`GPL-2.0-only WITH Linux-syscall-note`, matching Linux kernel UAPI convention.
