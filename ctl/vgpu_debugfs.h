/* SPDX-License-Identifier: GPL-2.0 */
#ifndef VGPU_DEBUGFS_H
#define VGPU_DEBUGFS_H

#include "vgpu_policy.h"

int vgpu_debugfs_init(struct vgpu_policy_table *policies);
void vgpu_debugfs_exit(void);

#endif /* VGPU_DEBUGFS_H */
