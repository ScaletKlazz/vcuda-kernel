/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VGPU_NV_IOCTL_H
#define VGPU_NV_IOCTL_H

#include <linux/types.h>

#include "vgpu_policy.h"

struct vgpu_nv_ioctl_status {
	bool open_hooked;
	bool release_hooked;
	bool ioctl_hooked;
	u32 hook_errors;
	u32 nvidia_major;
	u32 nvidia_uvm_major;
	bool memory_trace_enabled;
	bool compute_trace_enabled;
	u32 memory_alloc_ioctl_cmd;
	u32 memory_ioctl_class_offset;
	u32 memory_alloc_class;
	u32 memory_alloc_class2;
	u32 memory_alloc_type;
	u32 memory_alloc_flags;
	u32 memory_alloc_attr;
	u32 memory_alloc_attr2;
	u32 memory_alloc_tag;
	u64 memory_alloc_min_bytes;
	u64 memory_alloc_max_bytes;
	u32 memory_free_ioctl_cmd;
	u32 memory_free_object_offset;
	u32 memory_ioctl_size_offset;
	u32 memory_ioctl_nested_ptr_offset;
	u32 memory_ioctl_nested_size_offset;
	u64 memory_ioctl_size_filter_value;
	u64 memory_ioctl_size_max_bytes;
	u32 memory_ioctl_size_alignment;
	u32 ioctl_arg_sample_cmd;
	u32 rm_control_ioctl_cmd;
	u32 timeslice_control_cmd;
	u32 timeslice_us_offset;
	u64 timeslice_min_us;
	u64 timeslice_max_us;
};

int vgpu_nv_ioctl_init(struct vgpu_policy_table *policies, bool allow_enforce);
void vgpu_nv_ioctl_exit(void);
void vgpu_nv_ioctl_get_status(struct vgpu_nv_ioctl_status *out);

#endif /* VGPU_NV_IOCTL_H */
