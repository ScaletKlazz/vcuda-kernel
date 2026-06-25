/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VGPU_NV_570_SYMBOLS_H
#define VGPU_NV_570_SYMBOLS_H

/*
 * Fingerprint allowlist for NVIDIA 570.x OKM targets.
 *
 * M1 only enables probe-only capability. Memory and compute enforcement
 * symbols stay disabled until the exact allocation and TSG/GSP paths are
 * confirmed on the target driver build.
 */
#define VGPU_NV_SUPPORTED_MAJOR 570U

#endif /* VGPU_NV_570_SYMBOLS_H */
