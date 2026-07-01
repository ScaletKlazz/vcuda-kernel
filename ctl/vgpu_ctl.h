/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VGPU_CTL_H
#define VGPU_CTL_H

#include "vgpu_policy.h"

int vgpu_ctl_init(struct vgpu_policy_table *policies,
		  struct vgpu_cgroup_policy_table *cgroup_policies);
void vgpu_ctl_exit(void);

#endif /* VGPU_CTL_H */
