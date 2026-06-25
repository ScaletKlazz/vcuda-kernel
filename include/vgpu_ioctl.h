/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
#ifndef VGPU_IOCTL_H
#define VGPU_IOCTL_H

#include <linux/ioctl.h>

#include "vgpu_types.h"

#define VGPU_IOCTL_MAGIC 'V'

#define VGPU_IOCTL_SET_POLICY  _IOW(VGPU_IOCTL_MAGIC, 1, struct vgpu_policy)
#define VGPU_IOCTL_GET_POLICY  _IOWR(VGPU_IOCTL_MAGIC, 2, struct vgpu_policy_query)
#define VGPU_IOCTL_GET_STATS   _IOR(VGPU_IOCTL_MAGIC, 3, struct vgpu_stats)
#define VGPU_IOCTL_SET_DRY_RUN _IOW(VGPU_IOCTL_MAGIC, 4, int)
#define VGPU_IOCTL_DISABLE     _IO(VGPU_IOCTL_MAGIC, 5)

#endif /* VGPU_IOCTL_H */
