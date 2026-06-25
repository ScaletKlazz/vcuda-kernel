/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef VGPU_TYPES_H
#define VGPU_TYPES_H

#include <linux/types.h>

#define VGPU_POLICY_F_MEMORY  (1U << 0)
#define VGPU_POLICY_F_COMPUTE (1U << 1)
#define VGPU_POLICY_F_DRY_RUN (1U << 2)

#define VGPU_CAP_PROBE_ONLY       (1U << 0)
#define VGPU_CAP_MEMORY_DRY_RUN   (1U << 1)
#define VGPU_CAP_MEMORY_ENFORCE   (1U << 2)
#define VGPU_CAP_COMPUTE_DRY_RUN  (1U << 3)
#define VGPU_CAP_COMPUTE_ENFORCE  (1U << 4)

enum vgpu_runtime_mode {
	VGPU_MODE_DISABLED = 0,
	VGPU_MODE_TRACE_ONLY = 1,
	VGPU_MODE_DRY_RUN = 2,
	VGPU_MODE_ENFORCING = 3,
};

struct vgpu_policy {
	__s32 tgid;
	__s32 gpu_minor;
	__u64 memory_limit_bytes;
	__u32 compute_weight;
	__u32 flags;
};

struct vgpu_policy_query {
	__s32 tgid;
	__s32 gpu_minor;
	struct vgpu_policy policy;
	__u32 found;
	__u32 reserved;
};

struct vgpu_stats {
	__u64 policies_set;
	__u64 ioctl_seen;
	__u64 memory_alloc_seen;
	__u64 memory_free_seen;
	__u64 memory_would_deny;
	__u64 memory_denied;
	__u64 timeslice_seen;
	__u64 timeslice_would_rewrite;
	__u64 timeslice_rewritten;
	__u64 hook_errors;
	__u64 emergency_disabled;
	__u64 events_dropped;
	__u64 unmatched_free;
	__u32 runtime_mode;
	__u32 reserved;
};

struct vgpu_driver_fingerprint {
	__u32 driver_major;
	__u32 driver_minor;
	__u32 driver_patch;
	__u32 okm_detected;
	__u32 gsp_enabled;
	__u32 capabilities;
	__u32 reserved0;
	__u32 reserved1;
	__u64 symbol_hash;
};

#endif /* VGPU_TYPES_H */
