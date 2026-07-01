/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VGPU_POLICY_H
#define VGPU_POLICY_H

#include <linux/mutex.h>
#include <linux/rhashtable.h>

#include "vgpu_types.h"

struct vgpu_policy_key {
	__s32 tgid;
	__s32 gpu_minor;
};

struct vgpu_policy_entry {
	struct rhash_head node;
	struct vgpu_policy_key key;
	struct vgpu_policy policy;
};

struct vgpu_policy_table {
	struct rhashtable table;
	struct mutex lock;
};

struct vgpu_cgroup_policy_key {
	__u64 cgroup_id;
	__s32 gpu_minor;
	__u32 reserved;
};

struct vgpu_cgroup_policy_entry {
	struct rhash_head node;
	struct vgpu_cgroup_policy_key key;
	struct vgpu_cgroup_policy policy;
};

struct vgpu_cgroup_policy_table {
	struct rhashtable table;
	struct mutex lock;
};

int vgpu_policy_table_init(struct vgpu_policy_table *policies);
void vgpu_policy_table_destroy(struct vgpu_policy_table *policies);

int vgpu_policy_validate(const struct vgpu_policy *policy);
int vgpu_policy_set(struct vgpu_policy_table *policies,
		    const struct vgpu_policy *policy);
int vgpu_policy_get(struct vgpu_policy_table *policies, __s32 tgid,
		    __s32 gpu_minor, struct vgpu_policy *policy);
int vgpu_policy_for_each(struct vgpu_policy_table *policies,
			 int (*fn)(const struct vgpu_policy *policy, void *data),
			 void *data);

int vgpu_cgroup_policy_table_init(struct vgpu_cgroup_policy_table *policies);
void vgpu_cgroup_policy_table_destroy(struct vgpu_cgroup_policy_table *policies);
int vgpu_cgroup_policy_validate(const struct vgpu_cgroup_policy *policy);
int vgpu_cgroup_policy_set(struct vgpu_cgroup_policy_table *policies,
				   const struct vgpu_cgroup_policy *policy);
int vgpu_cgroup_policy_get(struct vgpu_cgroup_policy_table *policies,
				   __u64 cgroup_id, __s32 gpu_minor,
				   struct vgpu_cgroup_policy *policy);
int vgpu_cgroup_policy_for_each(struct vgpu_cgroup_policy_table *policies,
					int (*fn)(const struct vgpu_cgroup_policy *policy,
						  void *data),
					void *data);

#endif /* VGPU_POLICY_H */
