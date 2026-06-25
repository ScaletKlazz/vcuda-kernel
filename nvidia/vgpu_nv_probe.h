/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VGPU_NV_PROBE_H
#define VGPU_NV_PROBE_H

#include "vgpu_types.h"

int vgpu_nv_probe_init(void);
void vgpu_nv_probe_exit(void);
void vgpu_nv_probe_get_fingerprint(struct vgpu_driver_fingerprint *out);

#endif /* VGPU_NV_PROBE_H */
