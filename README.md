# vCUDA-kernel

[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel.svg?type=shield&issueType=license)](https://app.fossa.com/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel?ref=badge_shield&issueType=license)
![CodeQL](https://github.com/ScaletKlazz/vcuda-kernel/actions/workflows/codeQL.yml/badge.svg)
![Build](https://github.com/ScaletKlazz/vcuda-kernel/actions/workflows/kernel-build.yml/badge.svg)
[![Coverage](https://codecov.io/gh/ScaletKlazz/vcuda-kernel/graph/badge.svg)](https://codecov.io/gh/ScaletKlazz/vcuda-kernel)
![Issues](https://img.shields.io/github/issues/ScaletKlazz/vcuda-kernel)
![Release](https://img.shields.io/github/v/release/ScaletKlazz/vcuda-kernel?display_name=tag)
![License](https://img.shields.io/github/license/ScaletKlazz/vcuda-kernel)

Linux kernel enforcement module for vCUDA GPU virtualization.

`vCUDA-kernel` provides kernel-side NVIDIA GPU resource tracing, policy control,
memory accounting, and compute timeslice enforcement. It is designed to work as
the kernel component of the vCUDA stack together with `vCUDA-core` and the
Kubernetes device-plugin integration.

# HomePage

[CFN-Cloud](https://www.cfncloud.com)(In development...)

# Build Dependencies

- Linux host with matching kernel headers
- [NVIDIA Open GPU Kernel Modules](https://github.com/NVIDIA/open-gpu-kernel-modules) reference submodule
- NVIDIA 570/580 driver target environment
- GSP firmware enabled for the primary development path
- [CMake](https://cmake.org) >= 3.16
- `make` and `gcc`
- CUDA toolkit, only for example workloads
- root privileges for module load and unload

Ubuntu example:

```bash
sudo apt update
sudo apt install build-essential cmake linux-headers-$(uname -r)
```

# How to Use

## build

1. initialize submodules

```bash
git submodule update --init --recursive
```

2. build kernel module

```bash
make clean
make
```

3. build example workloads

```bash
make example
```

4. build with CMake wrapper

```bash
cmake -S . -B build
cmake --build build
```

## configure

```bash
# default dry-run load
make load

# enforcing compute timeslice rewrite
make reload DRY_RUN=0 ALLOW_ENFORCE=1 CLEAR_MEMORY_ON_LAST_CLOSE=0

# optional rewritten timeslice safety clamps
make reload DRY_RUN=0 ALLOW_ENFORCE=1 TIMESLICE_MIN_US=1000 TIMESLICE_MAX_US=10000
```

Useful load variables:

```text
DRY_RUN=1
ALLOW_ENFORCE=0
MEMORY_TRACE=1
COMPUTE_TRACE=1
MEMORY_ALLOC_MIN_BYTES=16777216
MEMORY_ALLOC_MAX_BYTES=0
MEMORY_TRACE_LIMIT_BYTES=0
CLEAR_MEMORY_ON_LAST_CLOSE=1
TIMESLICE_MIN_US=0
TIMESLICE_MAX_US=0
```

## usage

```bash
# inspect module state
make fingerprint
cat /sys/kernel/debug/vgpu/hooks
cat /sys/kernel/debug/vgpu/stats

# run CUDA allocation smoke test
./examples/cuda_malloc_smoke $((256 * 1024 * 1024)) 4 0

# run CUDA VMM smoke test
./examples/cuda_vmm_smoke $((256 * 1024 * 1024)) 4 0

# verify compute timeslice enforcement path
make verify-compute

# unload module
make unload
```

Debugfs files:

```text
/sys/kernel/debug/vgpu/enabled
/sys/kernel/debug/vgpu/driver_fingerprint
/sys/kernel/debug/vgpu/hooks
/sys/kernel/debug/vgpu/policies
/sys/kernel/debug/vgpu/tasks
/sys/kernel/debug/vgpu/events
/sys/kernel/debug/vgpu/timeslices
/sys/kernel/debug/vgpu/ioctls
/sys/kernel/debug/vgpu/stats
```

# Test

## module validation

```bash
make clean
make
make load
make fingerprint
nvidia-smi >/dev/null
cat /sys/kernel/debug/vgpu/stats
make unload
```

## example validation

```bash
make example
./examples/cuda_malloc_smoke $((256 * 1024 * 1024)) 4 0
./examples/cuda_vmm_smoke $((256 * 1024 * 1024)) 4 0
```

## compute enforcement validation

```bash
make verify-compute
make verify-compute VERIFY_COMPUTE_TIMESLICE_MIN_US=1000 VERIFY_COMPUTE_WEIGHT=1
make verify-compute VERIFY_COMPUTE_DRY_RUN=1 VERIFY_COMPUTE_ALLOW_ENFORCE=0
```

## KUnit

```bash
make test-kunit
```

# Features

## GPU Virtualization Features

### Base Features

- ✅ Linux kernel module: `vgpu-kernel.ko`
- ✅ NVIDIA driver fingerprint detection
- ✅ NVIDIA character-device open/ioctl/release tracing
- ✅ Debugfs diagnostics under `/sys/kernel/debug/vgpu`
- ✅ Control device: `/dev/vgpuctl`
- ✅ Per-task GPU context tracking
- ✅ Conservative GPU memory accounting
- ✅ CUDA VMM allocate/free object accounting
- ✅ RM_CONTROL tracing
- ✅ TSG timeslice dry-run tracing
- ✅ TSG timeslice rewrite enforcement
- ✅ KUnit test entrypoint
- ☐ Coverage report publishing
- ☐ cgroupfs policy control
- ☐ Kubernetes device-plugin integration
- ...

### More Features

- ☐ Remote GPU call over network
- ☐ GPU memory oversubscription control
- ☐ Multi-GPU policy scheduling
- ☐ GPU task hot snapshot
- ...

# Why This Project?

This project is built as the kernel enforcement layer for open GPU
virtualization experiments.

- Kernel Enforcement: move GPU resource control below user-space `LD_PRELOAD` boundaries.
- Open Architecture: keep driver ABI research, tracing, and enforcement logic auditable.
- Kubernetes Ready: provide a kernel foundation for device-plugin and cgroup policy injection.
- Dynamic Controllability: allow runtime task policy updates through `/dev/vgpuctl`.
- Dry-run First: validate policy decisions through debugfs before enabling write-capable paths.

`vCUDA-kernel` is intended to be published separately from:

- `vCUDA-core`: user-space CUDA virtualization and remote call transport.
- `vCUDA-device-plugin`: Kubernetes device-plugin, CDI, and cgroup policy injection.

# NVIDIA OKM Reference

This repository includes NVIDIA Open GPU Kernel Modules as a pinned reference
submodule under `third_party/open-gpu-kernel-modules`.

The submodule is used for ABI/layout lookup and RM ioctl structure references.
It is not built into `vgpu-kernel.ko`.

# Contributing

[Code of conduct](/CODE_OF_CONDUCT.md)

# License

[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel.svg?type=large&issueType=license)](https://app.fossa.com/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel?ref=badge_large&issueType=license)

GPL-2.0-only. Source files use SPDX license identifiers. UAPI headers use
`GPL-2.0-only WITH Linux-syscall-note`, matching Linux kernel UAPI convention.
