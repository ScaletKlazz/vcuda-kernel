/* SPDX-License-Identifier: GPL-2.0 */
#ifndef VGPU_IOCTL_ARG_H
#define VGPU_IOCTL_ARG_H

#include <linux/types.h>

#define VGPU_IOCTL_ARG_BYTES 1024U
#define VGPU_IOCTL_ARG_MAX 2048U
#define VGPU_IOCTL_ARG_VALUE_MAX 4096U

struct vgpu_ioctl_arg_snapshot {
	__u32 cmd;
	__u32 offset;
	__u64 count;
	__u64 filter_u32_hits;
	__u64 filter_u64_hits;
	__u32 last_u32;
	__u32 min_u32;
	__u32 max_u32;
	__u64 last_u64;
	__u64 min_u64;
	__u64 max_u64;
};

struct vgpu_ioctl_arg_value_snapshot {
	__u32 cmd;
	__u32 offset;
	__u32 value;
	__u64 count;
};

extern unsigned long vgpu_ioctl_arg_filter_value;
extern unsigned int vgpu_ioctl_arg_value_min_count;

void vgpu_ioctl_arg_init(void);
void vgpu_ioctl_arg_exit(void);
void vgpu_ioctl_arg_record(__u32 cmd, const void *bytes, size_t len);
void vgpu_ioctl_arg_sample_user(__u32 cmd, unsigned long user_ptr,
				       size_t len);
void vgpu_ioctl_arg_sample_user_ptr(__u32 cmd, unsigned long user_ptr,
				   size_t len, __u32 ptr_offset,
				   size_t nested_len);
bool vgpu_ioctl_arg_cmd_match(__u32 cmd, const __u32 *cmds, size_t count);
size_t vgpu_ioctl_arg_snapshot(struct vgpu_ioctl_arg_snapshot *out,
			       size_t max_records);
size_t vgpu_ioctl_arg_value_snapshot(struct vgpu_ioctl_arg_value_snapshot *out,
				     size_t max_records);

#ifdef CONFIG_KUNIT
void vgpu_ioctl_arg_reset_for_test(void);
#endif

#endif /* VGPU_IOCTL_ARG_H */
