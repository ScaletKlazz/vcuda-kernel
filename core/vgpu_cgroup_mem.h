/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VGPU_CGROUP_MEM_H
#define VGPU_CGROUP_MEM_H

#include <linux/types.h>

struct vgpu_cgroup_mem_snapshot {
	__u64 cgroup_id;
	__s32 gpu_minor;
	__u64 memory_used_bytes;
	__u64 alloc_seen;
	__u64 free_seen;
	__u64 would_deny;
	__u64 denied;
};

int vgpu_cgroup_mem_init(void);
void vgpu_cgroup_mem_exit(void);
int vgpu_cgroup_mem_charge(__u64 cgroup_id, __s32 gpu_minor, __u64 bytes,
				   __u64 limit_bytes, bool dry_run, bool *would_deny);
int vgpu_cgroup_mem_charge_object(__u64 cgroup_id, __s32 gpu_minor,
					  __u32 object, __u64 bytes,
					  __u64 limit_bytes, bool dry_run,
					  bool *would_deny);
int vgpu_cgroup_mem_uncharge(__u64 cgroup_id, __s32 gpu_minor, __u64 bytes);
int vgpu_cgroup_mem_uncharge_object(__u64 cgroup_id, __s32 gpu_minor,
					    __u32 object, __u64 *bytes_out);
int vgpu_cgroup_mem_for_each(
	int (*fn)(const struct vgpu_cgroup_mem_snapshot *snapshot, void *data),
	void *data);

#ifdef CONFIG_KUNIT
void vgpu_cgroup_mem_reset_for_test(void);
#endif

#endif /* VGPU_CGROUP_MEM_H */
