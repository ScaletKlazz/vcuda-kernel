# SPDX-License-Identifier: GPL-2.0-only

ifneq ($(KERNELRELEASE),)

obj-m += vgpu-kernel.o

vgpu-kernel-y := core/vgpu_main.o core/vgpu_device.o core/vgpu_ioctl_arg.o
vgpu-kernel-y += core/vgpu_ioctl_trace.o
vgpu-kernel-y += core/vgpu_policy.o
vgpu-kernel-y += core/vgpu_stats.o core/vgpu_task.o
vgpu-kernel-y += ctl/vgpu_ctl.o ctl/vgpu_debugfs.o ctl/vgpu_events.o
vgpu-kernel-y += nvidia/vgpu_nv_ioctl.o nvidia/vgpu_nv_probe.o

vgpu-kernel-$(CONFIG_VGPU_KERNEL_KUNIT_TEST) += tests/kunit/vgpu_policy_test.o
vgpu-kernel-$(CONFIG_VGPU_KERNEL_KUNIT_TEST) += tests/kunit/vgpu_ioctl_arg_test.o
vgpu-kernel-$(CONFIG_VGPU_KERNEL_KUNIT_TEST) += tests/kunit/vgpu_ioctl_trace_test.o
vgpu-kernel-$(CONFIG_VGPU_KERNEL_KUNIT_TEST) += tests/kunit/vgpu_task_test.o

ccflags-y += -I$(src)/include
ccflags-y += -I$(src)/core
ccflags-y += -I$(src)/ctl
ccflags-y += -I$(src)/nvidia
ccflags-y += -Wall

else

DRY_RUN ?= 1
ALLOW_ENFORCE ?= 0

# Enables NVIDIA RM allocation tracing in dry-run mode. Defaults to on because
# Step 1 validates conservative task-local GPU memory accounting.
MEMORY_TRACE ?= 1

# Conservative local-memory accounting gate. Source: observed CUDA/NVIDIA 570
# runtime creates many 2MiB internal RM local-memory objects; filtering below
# 16MiB keeps task-level accounting useful while still counting user-sized GPU
# allocations and larger runtime reservations.
MEMORY_ALLOC_MIN_BYTES ?= 16777216

# Optional upper bound for accounted allocations. 0 means no maximum. Useful in
# targeted tests when isolating one allocation size; not enabled by default.
MEMORY_ALLOC_MAX_BYTES ?= 0

# Optional dry-run limit used by vgpu_task_memory_charge() to emit would-deny
# events. 0 disables would-deny limit checks.
MEMORY_TRACE_LIMIT_BYTES ?= 0

# Clears task memory accounting when the last NVIDIA fd closes. Source: Step 1
# does not yet have a stable RM free path, so last-close cleanup prevents stale
# short-lived task entries in debugfs. Disable after free tracking is complete.
CLEAR_MEMORY_ON_LAST_CLOSE ?= 1

# Extracts OKM layout defaults from third_party/open-gpu-kernel-modules at load
# time. Values include NV_ESC_RM_ALLOC, NVOS21_PARAMETERS offsets, and
# NV_MEMORY_ALLOCATION_PARAMS.size offset. Override OKM-derived variables from
# make env only when validating a different driver ABI.
OKM_DEFAULTS_SCRIPT ?= scripts/extract_okm_defaults.sh

# Runtime-observed CUDA local-memory gates. These are not pure OKM constants:
# they are NV_MEMORY_ALLOCATION_PARAMS values observed for NVIDIA 570 CUDA local
# memory objects. Override from make env when a workload or driver branch differs.
MEMORY_ALLOC_TYPE ?= 0x00000000
MEMORY_ALLOC_FLAGS ?= 0x0001c101
MEMORY_ALLOC_ATTR ?= 0x18000000
MEMORY_ALLOC_ATTR2 ?= 0x00000000
MEMORY_IOCTL_SIZE_MAX_BYTES ?= 0
MEMORY_IOCTL_SIZE_ALIGNMENT ?= 4096

.PHONY: modules clean load unload reload fingerprint example example-clean

modules:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean

example:
	$(MAKE) -C examples

example-clean:
	$(MAKE) -C examples clean

load: modules
	@version=$$(grep -Eo '[0-9]+[.][0-9]+[.][0-9]+' /proc/driver/nvidia/version 2>/dev/null | head -n1); \
	if [ -z "$$version" ]; then version=$$(modinfo -F version nvidia 2>/dev/null | grep -Eo '[0-9]+[.][0-9]+[.][0-9]+' | head -n1); fi; \
	major=0; minor=0; patch=0; \
	if [ -n "$$version" ]; then \
		major=$${version%%.*}; rest=$${version#*.}; \
		if [ "$$rest" != "$$version" ]; then minor=$${rest%%.*}; rest=$${rest#*.}; fi; \
		if [ "$$rest" != "$$minor" ]; then patch=$${rest%%.*}; fi; \
	fi; \
	okm=0; \
	if grep -Eiq 'Open.*Kernel Module' /proc/driver/nvidia/version 2>/dev/null || modinfo -F license nvidia 2>/dev/null | grep -Eiq 'GPL|MIT'; then okm=1; fi; \
	gsp=0; \
	if awk -F: '/^EnableGpuFirmware/ { gsub(/[[:space:]]/, "", $$2); if ($$2 != "0" && $$2 != "") print "1"; }' /proc/driver/nvidia/params 2>/dev/null | grep -q 1; then gsp=1; fi; \
	nv_major=$$(awk '$$2 == "nvidia-frontend" || $$2 == "nvidia" { print $$1; exit }' /proc/devices 2>/dev/null); \
	uvm_major=$$(awk '$$2 == "nvidia-uvm" { print $$1; exit }' /proc/devices 2>/dev/null); \
	if [ -z "$$nv_major" ]; then nv_major=195; fi; \
	if [ -z "$$uvm_major" ]; then uvm_major=511; fi; \
	memory_args=""; \
	if [ "$(MEMORY_TRACE)" = "1" ]; then \
		okm_defaults=$$(sh "$(OKM_DEFAULTS_SCRIPT)" 2>/dev/null || true); \
		if [ -n "$$okm_defaults" ]; then eval "$$okm_defaults"; fi; \
		if [ -n "$(MEMORY_ALLOC_IOCTL_CMD)" ]; then MEMORY_ALLOC_IOCTL_CMD="$(MEMORY_ALLOC_IOCTL_CMD)"; fi; \
		if [ -n "$(MEMORY_IOCTL_CLASS_OFFSET)" ]; then MEMORY_IOCTL_CLASS_OFFSET="$(MEMORY_IOCTL_CLASS_OFFSET)"; fi; \
		if [ -n "$(MEMORY_ALLOC_CLASS)" ]; then MEMORY_ALLOC_CLASS="$(MEMORY_ALLOC_CLASS)"; fi; \
		if [ -n "$(MEMORY_IOCTL_NESTED_PTR_OFFSET)" ]; then MEMORY_IOCTL_NESTED_PTR_OFFSET="$(MEMORY_IOCTL_NESTED_PTR_OFFSET)"; fi; \
		if [ -n "$(MEMORY_IOCTL_NESTED_SIZE_OFFSET)" ]; then MEMORY_IOCTL_NESTED_SIZE_OFFSET="$(MEMORY_IOCTL_NESTED_SIZE_OFFSET)"; fi; \
		: "$${MEMORY_ALLOC_IOCTL_CMD:?missing MEMORY_ALLOC_IOCTL_CMD; initialize OKM submodule or override manually}"; \
		: "$${MEMORY_IOCTL_CLASS_OFFSET:?missing MEMORY_IOCTL_CLASS_OFFSET; initialize OKM submodule or override manually}"; \
		: "$${MEMORY_ALLOC_CLASS:?missing MEMORY_ALLOC_CLASS; initialize OKM submodule or override manually}"; \
		: "$${MEMORY_IOCTL_NESTED_PTR_OFFSET:?missing MEMORY_IOCTL_NESTED_PTR_OFFSET; initialize OKM submodule or override manually}"; \
		: "$${MEMORY_IOCTL_NESTED_SIZE_OFFSET:?missing MEMORY_IOCTL_NESTED_SIZE_OFFSET; initialize OKM submodule or override manually}"; \
		memory_args="memory_alloc_ioctl_cmd=$$MEMORY_ALLOC_IOCTL_CMD memory_ioctl_class_offset=$$MEMORY_IOCTL_CLASS_OFFSET memory_alloc_class=$$MEMORY_ALLOC_CLASS memory_alloc_type=$(MEMORY_ALLOC_TYPE) memory_alloc_flags=$(MEMORY_ALLOC_FLAGS) memory_alloc_attr=$(MEMORY_ALLOC_ATTR) memory_alloc_attr2=$(MEMORY_ALLOC_ATTR2) memory_ioctl_nested_ptr_offset=$$MEMORY_IOCTL_NESTED_PTR_OFFSET memory_ioctl_nested_size_offset=$$MEMORY_IOCTL_NESTED_SIZE_OFFSET memory_alloc_min_bytes=$(MEMORY_ALLOC_MIN_BYTES) memory_alloc_max_bytes=$(MEMORY_ALLOC_MAX_BYTES) memory_ioctl_size_max_bytes=$(MEMORY_IOCTL_SIZE_MAX_BYTES) memory_ioctl_size_alignment=$(MEMORY_IOCTL_SIZE_ALIGNMENT) memory_trace_limit_bytes=$(MEMORY_TRACE_LIMIT_BYTES)"; \
	fi; \
	echo "vgpu: loading nvidia_driver=$$major.$$minor.$$patch okm=$$okm gsp=$$gsp nvidia_major=$$nv_major nvidia_uvm_major=$$uvm_major dry_run=$(DRY_RUN) allow_enforce=$(ALLOW_ENFORCE) memory_trace=$(MEMORY_TRACE)"; \
	sudo insmod ./vgpu-kernel.ko dry_run=$(DRY_RUN) allow_enforce=$(ALLOW_ENFORCE) clear_memory_on_last_close=$(CLEAR_MEMORY_ON_LAST_CLOSE) nvidia_driver_major=$$major nvidia_driver_minor=$$minor nvidia_driver_patch=$$patch nvidia_okm_detected=$$okm nvidia_gsp_enabled=$$gsp nvidia_major=$$nv_major nvidia_uvm_major=$$uvm_major $$memory_args

unload:
	-sudo rmmod vgpu_kernel

reload: unload load

fingerprint:
	@cat /sys/kernel/debug/vgpu/driver_fingerprint

endif
