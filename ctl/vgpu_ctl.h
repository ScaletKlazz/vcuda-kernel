/* SPDX-License-Identifier: GPL-2.0 */
#ifndef VGPU_CTL_H
#define VGPU_CTL_H

#include "vgpu_policy.h"

int vgpu_ctl_init(struct vgpu_policy_table *policies);
void vgpu_ctl_exit(void);

#endif /* VGPU_CTL_H */
