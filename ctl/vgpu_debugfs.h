/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VGPU_DEBUGFS_H
#define VGPU_DEBUGFS_H

#include "vgpu_policy.h"

int vgpu_debugfs_init(struct vgpu_policy_table *policies,
		      struct vgpu_cgroup_policy_table *cgroup_policies);
void vgpu_debugfs_exit(void);

#endif /* VGPU_DEBUGFS_H */
