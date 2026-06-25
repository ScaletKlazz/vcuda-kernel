# SPDX-License-Identifier: GPL-2.0

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
	echo "vgpu: loading nvidia_driver=$$major.$$minor.$$patch okm=$$okm gsp=$$gsp nvidia_major=$$nv_major nvidia_uvm_major=$$uvm_major dry_run=$(DRY_RUN) allow_enforce=$(ALLOW_ENFORCE)"; \
	sudo insmod ./vgpu-kernel.ko dry_run=$(DRY_RUN) allow_enforce=$(ALLOW_ENFORCE) nvidia_driver_major=$$major nvidia_driver_minor=$$minor nvidia_driver_patch=$$patch nvidia_okm_detected=$$okm nvidia_gsp_enabled=$$gsp nvidia_major=$$nv_major nvidia_uvm_major=$$uvm_major

unload:
	-sudo rmmod vgpu_kernel

reload: unload load

fingerprint:
	@cat /sys/kernel/debug/vgpu/driver_fingerprint

endif
