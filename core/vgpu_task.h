/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VGPU_TASK_H
#define VGPU_TASK_H

#include <linux/types.h>

struct vgpu_task_snapshot {
	__s32 pid;
	__s32 tgid;
	__s32 gpu_minor;
	__u32 nvidia_major;
	__u32 fd_refs;
	__u64 memory_used_bytes;
	__u64 last_timeslice;
	__u64 last_seen_jiffies;
};

int vgpu_task_registry_init(void);
void vgpu_task_registry_exit(void);
int vgpu_task_open(__s32 pid, __s32 tgid, __s32 gpu_minor,
		   __u32 nvidia_major);
void vgpu_task_touch(__s32 pid, __s32 tgid, __s32 gpu_minor,
		     __u32 nvidia_major);
void vgpu_task_close(__s32 tgid, __s32 gpu_minor);
int vgpu_task_memory_charge(__s32 pid, __s32 tgid, __s32 gpu_minor,
			    __u32 nvidia_major, __u64 bytes,
			    __u64 limit_bytes, bool dry_run,
			    bool *would_deny);
int vgpu_task_memory_uncharge(__s32 tgid, __s32 gpu_minor, __u64 bytes);
int vgpu_task_for_each(int (*fn)(const struct vgpu_task_snapshot *task,
				 void *data),
		       void *data);

#ifdef CONFIG_KUNIT
void vgpu_task_reset_for_test(void);
#endif

#endif /* VGPU_TASK_H */
