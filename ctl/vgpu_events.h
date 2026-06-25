/* SPDX-License-Identifier: GPL-2.0 */
#ifndef VGPU_EVENTS_H
#define VGPU_EVENTS_H

#include <linux/types.h>

#define VGPU_EVENT_RING_SIZE 256U

enum vgpu_event_type {
	VGPU_EVENT_POLICY_SET = 1,
	VGPU_EVENT_IOCTL_SEEN,
	VGPU_EVENT_MEMORY_ALLOC,
	VGPU_EVENT_MEMORY_FREE,
	VGPU_EVENT_MEMORY_WOULD_DENY,
	VGPU_EVENT_MEMORY_DENIED,
	VGPU_EVENT_TIMESLICE_SEEN,
	VGPU_EVENT_TIMESLICE_WOULD_REWRITE,
	VGPU_EVENT_TIMESLICE_REWRITTEN,
	VGPU_EVENT_HOOK_ERROR,
	VGPU_EVENT_EMERGENCY_DISABLED,
	VGPU_EVENT_NVIDIA_OPEN,
	VGPU_EVENT_NVIDIA_RELEASE,
};

struct vgpu_event_record {
	__u64 seq;
	__u64 ts_ns;
	__u32 type;
	__s32 pid;
	__s32 tgid;
	__s32 gpu_minor;
	__u64 old_value;
	__u64 new_value;
	__s32 error;
	__u32 flags;
};

void vgpu_events_init(void);
void vgpu_events_push(enum vgpu_event_type type, __s32 pid, __s32 tgid,
		      __s32 gpu_minor, __u64 old_value, __u64 new_value,
		      __s32 error, __u32 flags);
size_t vgpu_events_snapshot(struct vgpu_event_record *out, size_t max_records);

#ifdef CONFIG_KUNIT
void vgpu_events_reset_for_test(void);
#endif

#endif /* VGPU_EVENTS_H */
