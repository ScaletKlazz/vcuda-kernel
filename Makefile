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
vgpu-kernel-$(CONFIG_VGPU_KERNEL_KUNIT_TEST) += tests/kunit/vgpu_events_test.o
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

# Enables RM_CONTROL dry-run tracing for compute scheduling. Source: OKM
# NV_ESC_RM_CONTROL + NVA06C_CTRL_CMD_SET_TIMESLICE. This only observes
# timesliceUs and emits would-rewrite counters; it does not mutate user params.
COMPUTE_TRACE ?= 1

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
# time. Values include NV_ESC_RM_ALLOC, NV_ESC_RM_FREE, NVOS00/NVOS21 offsets, and
# NV_MEMORY_ALLOCATION_PARAMS.size offset. Override OKM-derived variables from
# make env only when validating a different driver ABI.
OKM_DEFAULTS_SCRIPT ?= scripts/extract_okm_defaults.sh

# Optional RM allocation gates. Defaults are broad because VMM alloc/free uses
# several RM classes; object-handle tracking plus MEMORY_ALLOC_MIN_BYTES is the
# primary filter. Set MEMORY_ALLOC_CLASS=0x40 and detail gates for local-only tests.
MEMORY_ALLOC_CLASS ?= 0x00000040
MEMORY_ALLOC_CLASS2 ?= 0x0000003e
MEMORY_ALLOC_TYPE ?= 0xffffffff
MEMORY_ALLOC_FLAGS ?= 0xffffffff
MEMORY_ALLOC_ATTR ?= 0xffffffff
MEMORY_ALLOC_ATTR2 ?= 0xffffffff
MEMORY_IOCTL_SIZE_MAX_BYTES ?= 0
MEMORY_IOCTL_SIZE_ALIGNMENT ?= 4096

# Optional compute trace overrides. Defaults come from OKM when available;
# fallback values match NVIDIA 570/580 observed RM ABI and ctrla06c.h.
RM_CONTROL_IOCTL_CMD ?=
TIMESLICE_CONTROL_CMD ?=
TIMESLICE_US_OFFSET ?=

# Optional safety clamps for rewritten timesliceUs. Source: operator policy,
# not OKM. 0 disables the clamp so default behavior preserves raw weight math.
TIMESLICE_MIN_US ?= 0
TIMESLICE_MAX_US ?= 0

.PHONY: modules clean load unload reload fingerprint example example-clean verify-compute test-kunit

modules:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean

example:
	$(MAKE) -C examples

example-clean:
	$(MAKE) -C examples clean

# Runs a repeatable compute timeslice validation. Defaults enforce a 50% policy
# and verify stats plus /sys/kernel/debug/vgpu/timeslices evidence.
VERIFY_COMPUTE_DRY_RUN ?= 0
VERIFY_COMPUTE_ALLOW_ENFORCE ?= 1
VERIFY_COMPUTE_TIMESLICE_MIN_US ?= 0
VERIFY_COMPUTE_TIMESLICE_MAX_US ?= 0

verify-compute: example
	$(MAKE) reload DRY_RUN=$(VERIFY_COMPUTE_DRY_RUN) ALLOW_ENFORCE=$(VERIFY_COMPUTE_ALLOW_ENFORCE) CLEAR_MEMORY_ON_LAST_CLOSE=0 TIMESLICE_MIN_US=$(VERIFY_COMPUTE_TIMESLICE_MIN_US) TIMESLICE_MAX_US=$(VERIFY_COMPUTE_TIMESLICE_MAX_US)
	sh scripts/verify_compute.sh

test-kunit:
	kunit.py run --kunitconfig=tests/kunit/.kunitconfig

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
	compute_args=""; \
	okm_defaults=""; \
	if [ "$(MEMORY_TRACE)" = "1" ] || [ "$(COMPUTE_TRACE)" = "1" ]; then \
		okm_defaults=$$(sh "$(OKM_DEFAULTS_SCRIPT)" || true); \
		if [ -n "$$okm_defaults" ]; then eval "$$okm_defaults"; fi; \
	fi; \
	if [ "$(MEMORY_TRACE)" = "1" ]; then \
		if [ -z "$$MEMORY_ALLOC_IOCTL_CMD" ]; then MEMORY_ALLOC_IOCTL_CMD=0xc030462b; fi; \
		if [ -z "$$MEMORY_FREE_IOCTL_CMD" ]; then MEMORY_FREE_IOCTL_CMD=0xc0104629; fi; \
		if [ -z "$$MEMORY_FREE_OBJECT_OFFSET" ]; then MEMORY_FREE_OBJECT_OFFSET=8; fi; \
		if [ -z "$$MEMORY_IOCTL_CLASS_OFFSET" ]; then MEMORY_IOCTL_CLASS_OFFSET=12; fi; \
		if [ -z "$$MEMORY_ALLOC_CLASS" ]; then MEMORY_ALLOC_CLASS=0x00000040; fi; \
		if [ -z "$$MEMORY_IOCTL_NESTED_PTR_OFFSET" ]; then MEMORY_IOCTL_NESTED_PTR_OFFSET=16; fi; \
		if [ -z "$$MEMORY_IOCTL_NESTED_SIZE_OFFSET" ]; then MEMORY_IOCTL_NESTED_SIZE_OFFSET=64; fi; \
		if [ -n "$(MEMORY_ALLOC_IOCTL_CMD)" ]; then MEMORY_ALLOC_IOCTL_CMD="$(MEMORY_ALLOC_IOCTL_CMD)"; fi; \
		if [ -n "$(MEMORY_FREE_IOCTL_CMD)" ]; then MEMORY_FREE_IOCTL_CMD="$(MEMORY_FREE_IOCTL_CMD)"; fi; \
		if [ -n "$(MEMORY_FREE_OBJECT_OFFSET)" ]; then MEMORY_FREE_OBJECT_OFFSET="$(MEMORY_FREE_OBJECT_OFFSET)"; fi; \
		if [ -n "$(MEMORY_IOCTL_CLASS_OFFSET)" ]; then MEMORY_IOCTL_CLASS_OFFSET="$(MEMORY_IOCTL_CLASS_OFFSET)"; fi; \
		if [ -n "$(MEMORY_ALLOC_CLASS)" ]; then MEMORY_ALLOC_CLASS="$(MEMORY_ALLOC_CLASS)"; fi; \
		if [ -n "$(MEMORY_ALLOC_CLASS2)" ]; then MEMORY_ALLOC_CLASS2="$(MEMORY_ALLOC_CLASS2)"; fi; \
		if [ -n "$(MEMORY_IOCTL_NESTED_PTR_OFFSET)" ]; then MEMORY_IOCTL_NESTED_PTR_OFFSET="$(MEMORY_IOCTL_NESTED_PTR_OFFSET)"; fi; \
		if [ -n "$(MEMORY_IOCTL_NESTED_SIZE_OFFSET)" ]; then MEMORY_IOCTL_NESTED_SIZE_OFFSET="$(MEMORY_IOCTL_NESTED_SIZE_OFFSET)"; fi; \
		: "$${MEMORY_ALLOC_IOCTL_CMD:?missing MEMORY_ALLOC_IOCTL_CMD; initialize OKM submodule or override manually}"; \
		: "$${MEMORY_FREE_IOCTL_CMD:?missing MEMORY_FREE_IOCTL_CMD; initialize OKM submodule or override manually}"; \
		: "$${MEMORY_FREE_OBJECT_OFFSET:?missing MEMORY_FREE_OBJECT_OFFSET; initialize OKM submodule or override manually}"; \
		: "$${MEMORY_IOCTL_CLASS_OFFSET:?missing MEMORY_IOCTL_CLASS_OFFSET; initialize OKM submodule or override manually}"; \
		: "$${MEMORY_ALLOC_CLASS:?missing MEMORY_ALLOC_CLASS; initialize OKM submodule or override manually}"; \
		: "$${MEMORY_IOCTL_NESTED_PTR_OFFSET:?missing MEMORY_IOCTL_NESTED_PTR_OFFSET; initialize OKM submodule or override manually}"; \
		: "$${MEMORY_IOCTL_NESTED_SIZE_OFFSET:?missing MEMORY_IOCTL_NESTED_SIZE_OFFSET; initialize OKM submodule or override manually}"; \
		memory_args="memory_alloc_ioctl_cmd=$$MEMORY_ALLOC_IOCTL_CMD memory_free_ioctl_cmd=$$MEMORY_FREE_IOCTL_CMD memory_free_object_offset=$$MEMORY_FREE_OBJECT_OFFSET memory_ioctl_class_offset=$$MEMORY_IOCTL_CLASS_OFFSET memory_alloc_class=$$MEMORY_ALLOC_CLASS memory_alloc_class2=$(MEMORY_ALLOC_CLASS2) memory_alloc_type=$(MEMORY_ALLOC_TYPE) memory_alloc_flags=$(MEMORY_ALLOC_FLAGS) memory_alloc_attr=$(MEMORY_ALLOC_ATTR) memory_alloc_attr2=$(MEMORY_ALLOC_ATTR2) memory_ioctl_nested_ptr_offset=$$MEMORY_IOCTL_NESTED_PTR_OFFSET memory_ioctl_nested_size_offset=$$MEMORY_IOCTL_NESTED_SIZE_OFFSET memory_alloc_min_bytes=$(MEMORY_ALLOC_MIN_BYTES) memory_alloc_max_bytes=$(MEMORY_ALLOC_MAX_BYTES) memory_ioctl_size_max_bytes=$(MEMORY_IOCTL_SIZE_MAX_BYTES) memory_ioctl_size_alignment=$(MEMORY_IOCTL_SIZE_ALIGNMENT) memory_trace_limit_bytes=$(MEMORY_TRACE_LIMIT_BYTES)"; \
	fi; \
	if [ "$(COMPUTE_TRACE)" = "1" ]; then \
		if [ -z "$$RM_CONTROL_IOCTL_CMD" ]; then RM_CONTROL_IOCTL_CMD=0xc020462a; fi; \
		if [ -z "$$TIMESLICE_CONTROL_CMD" ]; then TIMESLICE_CONTROL_CMD=0xa06c0103; fi; \
		if [ -z "$$TIMESLICE_US_OFFSET" ]; then TIMESLICE_US_OFFSET=0; fi; \
		if [ -n "$(RM_CONTROL_IOCTL_CMD)" ]; then RM_CONTROL_IOCTL_CMD="$(RM_CONTROL_IOCTL_CMD)"; fi; \
		if [ -n "$(TIMESLICE_CONTROL_CMD)" ]; then TIMESLICE_CONTROL_CMD="$(TIMESLICE_CONTROL_CMD)"; fi; \
		if [ -n "$(TIMESLICE_US_OFFSET)" ]; then TIMESLICE_US_OFFSET="$(TIMESLICE_US_OFFSET)"; fi; \
		compute_args="rm_control_ioctl_cmd=$$RM_CONTROL_IOCTL_CMD timeslice_control_cmd=$$TIMESLICE_CONTROL_CMD timeslice_us_offset=$$TIMESLICE_US_OFFSET timeslice_min_us=$(TIMESLICE_MIN_US) timeslice_max_us=$(TIMESLICE_MAX_US)"; \
	fi; \
	echo "vgpu: loading nvidia_driver=$$major.$$minor.$$patch okm=$$okm gsp=$$gsp nvidia_major=$$nv_major nvidia_uvm_major=$$uvm_major dry_run=$(DRY_RUN) allow_enforce=$(ALLOW_ENFORCE) memory_trace=$(MEMORY_TRACE) compute_trace=$(COMPUTE_TRACE)"; \
	sudo insmod ./vgpu-kernel.ko dry_run=$(DRY_RUN) allow_enforce=$(ALLOW_ENFORCE) clear_memory_on_last_close=$(CLEAR_MEMORY_ON_LAST_CLOSE) nvidia_driver_major=$$major nvidia_driver_minor=$$minor nvidia_driver_patch=$$patch nvidia_okm_detected=$$okm nvidia_gsp_enabled=$$gsp nvidia_major=$$nv_major nvidia_uvm_major=$$uvm_major $$memory_args $$compute_args

unload:
	-sudo rmmod vgpu_kernel

reload: unload load

fingerprint:
	@cat /sys/kernel/debug/vgpu/driver_fingerprint

endif
