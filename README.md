# vCUDA-kernel

[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel.svg?type=shield&issueType=license)](https://app.fossa.com/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel?ref=badge_shield&issueType=license)
![CodeQL](https://github.com/ScaletKlazz/vcuda-kernel/actions/workflows/codeQL.yml/badge.svg)
![Build](https://github.com/ScaletKlazz/vcuda-kernel/actions/workflows/kernel-build.yml/badge.svg)
![Issues](https://img.shields.io/github/issues/ScaletKlazz/vcuda-kernel)
![Release](https://img.shields.io/github/v/release/ScaletKlazz/vcuda-kernel?display_name=tag)
![License](https://img.shields.io/github/license/ScaletKlazz/vcuda-kernel)

`vCUDA-kernel` is the kernel-side enforcement component of the vCUDA project.
It is intended to be published as a standalone repository, separate from the
future `device-plugin` and `vcuda-core` repositories.

The module currently targets NVIDIA 570.x drivers on Linux and starts in
trace-only / dry-run mode by default. It does not rely on `LD_PRELOAD` for
enforcement decisions.

## Status

Implemented:

- `vgpu-kernel.ko` Linux kernel module.
- `/dev/vgpuctl` control device.
- read-only debugfs diagnostics under `/sys/kernel/debug/vgpu`.
- NVIDIA driver fingerprint injection at load time.
- trace-only NVIDIA character-device open, ioctl, and release probes.
- active `(tgid, gpu_minor)` task context tracking.
- GPU memory usage accounting core.
- optional configurable memory ioctl trace accounting.

In progress:

- NVIDIA 570.x memory allocation/free ioctl identification.
- GPU memory allocation denial.
- NVIDIA context/channel/TSG mapping.
- TSG timeSlice dry-run and enforcement.

Out of scope for this repository:

- Kubernetes device-plugin and CDI integration.
- cgroupfs policy surface.
- remote CUDA RPC and user-space CUDA virtualization.

Those pieces are planned as separate repositories.

## Requirements

- Linux host with matching kernel headers installed.
- NVIDIA 570.x driver.
- NVIDIA Open Kernel Modules recommended.
- GSP firmware enabled for the primary target path.
- `make`, `gcc`, `cmake` optional.
- root privileges for module load/unload.

Ubuntu example:

```bash
sudo apt update
sudo apt install build-essential cmake linux-headers-$(uname -r)
```

If kernel module build fails with unresolved `module_layout`, check that
`/lib/modules/$(uname -r)/build/Module.symvers` is not empty.

## Build With Make

Clone with the NVIDIA Open GPU Kernel Modules reference submodule:

```bash
git submodule update --init --recursive
```

The submodule is checked in at NVIDIA `570.211.01` under
`third_party/open-gpu-kernel-modules`. It is a reference source tree for RM
ioctl layouts and class/control definitions; it is not built into this module.

```bash
make
```

Equivalent explicit Kbuild command:

```bash
make -C /lib/modules/$(uname -r)/build M=$PWD modules
```

Clean:

```bash
make clean
```

## Build With CMake

CMake is a wrapper around the existing Kbuild `Makefile`. It does not replace
Kbuild.

```bash
cmake -S . -B build
cmake --build build
```

Useful CMake targets:

```bash
cmake --build build --target load
cmake --build build --target unload
cmake --build build --target reload
cmake --build build --target fingerprint
cmake --build build --target debug-hooks
cmake --build build --target debug-tasks
cmake --build build --target debug-stats
cmake --build build --target debug-ioctls
cmake --build build --target debug-ioctl-args
cmake --build build --target debug-events
cmake --build build --target debug-logs
```

Module load options can be set at configure time:

```bash
cmake -S . -B build -DVCUDA_DRY_RUN=1 -DVCUDA_ALLOW_ENFORCE=0
cmake --build build --target load
```

## Load

The default load path is safe and non-enforcing:

```bash
make load
```

`make load` automatically reads NVIDIA version and device-major data from the
host and passes them to `insmod`.

Expected log line:

```text
vgpu: loading nvidia_driver=570.x.y okm=1 gsp=1 nvidia_major=195 nvidia_uvm_major=511 dry_run=1 allow_enforce=0
```

Unload:

```bash
make unload
```

Reload:

```bash
make reload
```

## Debugfs

Mount debugfs if needed:

```bash
sudo mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
```

Available files:

```text
/sys/kernel/debug/vgpu/enabled
/sys/kernel/debug/vgpu/driver_fingerprint
/sys/kernel/debug/vgpu/hooks
/sys/kernel/debug/vgpu/policies
/sys/kernel/debug/vgpu/tasks
/sys/kernel/debug/vgpu/events
/sys/kernel/debug/vgpu/ioctls
/sys/kernel/debug/vgpu/ioctl_args
/sys/kernel/debug/vgpu/ioctl_arg_values
/sys/kernel/debug/vgpu/rm_controls
/sys/kernel/debug/vgpu/stats
```

Quick checks:

```bash
make fingerprint
cat /sys/kernel/debug/vgpu/hooks
cat /sys/kernel/debug/vgpu/tasks
cat /sys/kernel/debug/vgpu/ioctls
cat /sys/kernel/debug/vgpu/ioctl_args
cat /sys/kernel/debug/vgpu/ioctl_arg_values
cat /sys/kernel/debug/vgpu/rm_controls
cat /sys/kernel/debug/vgpu/stats
cat /sys/kernel/debug/vgpu/events | tail -n 80
```

## Basic Validation

Build and load:

```bash
make clean
make
make load
```

Check fingerprint:

```bash
make fingerprint
```

Expected for NVIDIA 570.x:

```text
driver_major=570 ... capabilities=1 ... probe_only=1
```

Trigger device tracing:

```bash
nvidia-smi >/dev/null
cat /sys/kernel/debug/vgpu/stats
cat /sys/kernel/debug/vgpu/events | tail -n 30
```

Expected:

- `ioctl_seen` increases.
- event `type=2` appears for ioctl.
- event `type=12` appears for NVIDIA open.
- event `type=13` appears for NVIDIA release.
- `/sys/kernel/debug/vgpu/ioctls` shows aggregated ioctl `cmd` counts.
- `hook_errors=0`.

Track active task contexts:

```bash
nvidia-smi -l 1 >/tmp/nvsmi.log 2>&1 &
pid=$!
sleep 2
cat /sys/kernel/debug/vgpu/tasks
kill $pid
sleep 1
cat /sys/kernel/debug/vgpu/tasks
```

Expected:

- running process appears with `tgid`, `gpu_minor`, and `fd_refs`;
- context disappears or decreases after process exit.

Trace memory accounting with a known ioctl command:

```bash
sudo rmmod vgpu_kernel
sudo insmod ./vgpu-kernel.ko \
  dry_run=1 \
  memory_alloc_ioctl_cmd=0xc030462b \
  memory_ioctl_nested_ptr_offset=16 \
  memory_ioctl_nested_size_offset=64 \
  memory_ioctl_size_filter_value=$((256 * 1024 * 1024)) \
  memory_ioctl_size_max_bytes=$((1024 * 1024 * 1024)) \
  memory_ioctl_size_alignment=4096 \
  memory_trace_limit_bytes=$((1024 * 1024 * 1024))
make example
./examples/cuda_malloc_smoke $((256 * 1024 * 1024)) 4 0
cat /sys/kernel/debug/vgpu/stats
cat /sys/kernel/debug/vgpu/tasks
cat /sys/kernel/debug/vgpu/events | tail -n 80
```

`memory_alloc_ioctl_cmd` and `memory_free_ioctl_cmd` are disabled by default.
Use `memory_ioctl_arg_is_size=1` only when `arg` is the byte size directly.
Use `memory_ioctl_size_offset` when `arg` points to a structure containing a
u64 byte size field inside the ioctl argument itself.
Use `memory_ioctl_nested_ptr_offset` and `memory_ioctl_nested_size_offset` when
the ioctl argument points to a wrapper containing another parameter pointer.

To find candidate NVIDIA 570.x memory commands, compare ioctl snapshots around
a focused allocation workload:

```bash
make example
cat /sys/kernel/debug/vgpu/ioctls >/tmp/vgpu-ioctls.before
./examples/cuda_malloc_smoke $((256 * 1024 * 1024)) 4 0
cat /sys/kernel/debug/vgpu/ioctls >/tmp/vgpu-ioctls.after
diff -u /tmp/vgpu-ioctls.before /tmp/vgpu-ioctls.after
```

Then sample one candidate ioctl argument structure. Example:

```bash
sudo rmmod vgpu_kernel
sudo insmod ./vgpu-kernel.ko dry_run=1 \
  ioctl_arg_sample_cmds="0xc030462b" \
  ioctl_arg_nested_ptr_offset=16 \
  ioctl_arg_nested_sample_bytes=1024 \
  ioctl_arg_filter_value=$((256 * 1024 * 1024))
make example
./examples/cuda_malloc_smoke $((256 * 1024 * 1024)) 4 0
cat /sys/kernel/debug/vgpu/ioctl_args
grep -E '268435456|536870912' /sys/kernel/debug/vgpu/ioctl_args
cat /sys/kernel/debug/vgpu/rm_controls | sort -k4,4nr | head -n 80
```

When `ioctl_arg_filter_value` is set, `ioctl_args` includes exact
`filter_u32_hits` and `filter_u64_hits` counters. Prefer offsets whose hit count
tracks the number of expected allocation calls. For `cmd=0xc030462b`,
`_IOC_SIZE(cmd)` is only `0x30`; offsets beyond that are outside the ioctl
argument and may be stack residue. Use nested sampling through pointer offsets
before enabling memory accounting for this path.
For the current NVIDIA 570 trace, `cmd=0xc030462b`, wrapper pointer offset `16`,
and nested size offset `64` track the tested allocation count and size.

The NVIDIA Open GPU Kernel Modules reference explains why:

- `NV_ESC_RM_ALLOC` is escape `0x2b`, which matches ioctl `0xc030462b`.
- The user argument is `NVOS21_PARAMETERS`.
- `NVOS21_PARAMETERS.pAllocParms` is at offset `16`.
- For memory classes using `NV_MEMORY_ALLOCATION_PARAMS`, `size` is at offset
  `64` in the nested allocation parameter buffer.

Reference files:

```text
third_party/open-gpu-kernel-modules/src/nvidia/arch/nvalloc/unix/include/nv_escape.h
third_party/open-gpu-kernel-modules/src/common/sdk/nvidia/inc/nvos.h
third_party/open-gpu-kernel-modules/src/common/sdk/nvidia/inc/class/
```

To avoid tying accounting to one allocation size, identify the stable RM control
field first. `rm_controls` is a compact u32 value histogram from the sampled
nested buffer. Compare it across different allocation sizes and loop counts:

```bash
sudo rmmod vgpu_kernel 2>/dev/null || true
sudo insmod ./vgpu-kernel.ko dry_run=1 \
  ioctl_arg_sample_cmds="0xc030462b" \
  ioctl_arg_nested_ptr_offset=16 \
  ioctl_arg_nested_sample_bytes=1024 \
  ioctl_arg_value_min_count=4
make example
./examples/cuda_malloc_smoke $((256 * 1024 * 1024)) 4 0
cat /sys/kernel/debug/vgpu/rm_controls >/tmp/vgpu-rm-256.txt
sudo rmmod vgpu_kernel
sudo insmod ./vgpu-kernel.ko dry_run=1 \
  ioctl_arg_sample_cmds="0xc030462b" \
  ioctl_arg_nested_ptr_offset=16 \
  ioctl_arg_nested_sample_bytes=1024 \
  ioctl_arg_value_min_count=4
./examples/cuda_malloc_smoke $((512 * 1024 * 1024)) 4 0
cat /sys/kernel/debug/vgpu/rm_controls >/tmp/vgpu-rm-512.txt
diff -u /tmp/vgpu-rm-256.txt /tmp/vgpu-rm-512.txt | head -n 120
```

Candidate RM control fields keep the same `(offset, value)` across both runs,
while size fields change from `268435456` to `536870912`. After the control
field is confirmed, memory accounting should match on `(cmd, control_offset,
control_value)` and then read the byte size from `memory_ioctl_nested_size_offset`;
`memory_ioctl_size_filter_value` should remain diagnostic only.

Unload:

```bash
make unload
dmesg | tail -n 30
```

Expected:

- `vgpu: module unloaded`;
- no `Oops`, `BUG`, or kernel warning.

## Control Device

`/dev/vgpuctl` is the stable control surface for future policy injection.
Current policy support is PID/TGID scoped.

The first enforcement-capable path will still require:

- supported fingerprint;
- explicit policy;
- `dry_run=0`;
- `allow_enforce=1`.

By default, the module does not deny memory allocations and does not rewrite
NVIDIA scheduling state.

## Safety Model

- Default mode is dry-run.
- `allow_enforce=0` by default.
- Trace hooks do not modify NVIDIA driver return values.
- Unsupported fingerprints must not enable enforcement capability.
- Hook registration failure records `hook_errors` and keeps module load safe.
- Module unload removes kprobes before releasing registries.

## Roadmap

1. NVIDIA 570.x memory ioctl fingerprinting.
2. Memory allocation denial after allocation-path fingerprinting.
3. NVIDIA context/channel/TSG mapping.
4. TSG timeSlice dry-run.
5. TSG timeSlice enforcement behind fingerprint gates.
6. cgroupfs and Kubernetes device-plugin integration in separate repository.
7. remote CUDA RPC in separate `vcuda-core` repository.

## Repository Split

Recommended public repositories:

- `vCUDA-kernel`: this repository, kernel-side enforcement.
- `vCUDA-device-plugin`: Kubernetes device-plugin, CDI, cgroup policy injection.
- `vCUDA-core`: user-space CUDA virtualization and remote call transport.

## License

[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel.svg?type=large&issueType=license)](https://app.fossa.com/projects/git%2Bgithub.com%2FScaletKlazz%2Fvcuda-kernel?ref=badge_large&issueType=license)

GPL-2.0. Source files use SPDX license identifiers. UAPI headers use
`GPL-2.0 WITH Linux-syscall-note`, matching Linux kernel UAPI convention.

## FOSSA Tips

- Scan this repository with submodules initialized so FOSSA can see the pinned
  NVIDIA Open GPU Kernel Modules reference tree:

  ```bash
  git submodule update --init --recursive
  fossa analyze
  ```

- Treat `third_party/open-gpu-kernel-modules` as a reference dependency, not as
  code linked into `vgpu-kernel.ko`. It is pinned for ABI/layout lookup and RM
  ioctl structure documentation.
- Keep SPDX headers on every source file. Kernel implementation files should use
  `GPL-2.0`; UAPI headers should keep `GPL-2.0 WITH Linux-syscall-note`.
- Do not vendor generated build artifacts into scans. Kernel outputs such as
  `*.ko`, `*.o`, `*.mod*`, `.tmp_versions/`, `Module.symvers`, and CMake build
  directories are ignored by `.gitignore`.
- If FOSSA reports the NVIDIA submodule separately, review it as third-party
  reference source and keep its upstream license metadata unchanged.
