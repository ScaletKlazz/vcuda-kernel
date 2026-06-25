/* SPDX-License-Identifier: GPL-2.0 */
#ifndef VGPU_IOCTL_TRACE_H
#define VGPU_IOCTL_TRACE_H

#include <linux/types.h>

#define VGPU_IOCTL_TRACE_MAX 256U

struct vgpu_ioctl_trace_snapshot {
	__u32 cmd;
	__u64 count;
	__u64 last_arg;
	__s32 last_pid;
	__s32 last_tgid;
	__s32 last_gpu_minor;
	__u32 last_nvidia_major;
};

void vgpu_ioctl_trace_init(void);
void vgpu_ioctl_trace_record(__u32 cmd, __s32 pid, __s32 tgid,
			     __s32 gpu_minor, __u32 nvidia_major,
			     __u64 arg);
size_t vgpu_ioctl_trace_snapshot(struct vgpu_ioctl_trace_snapshot *out,
				 size_t max_records);

#ifdef CONFIG_KUNIT
void vgpu_ioctl_trace_reset_for_test(void);
#endif

#endif /* VGPU_IOCTL_TRACE_H */
