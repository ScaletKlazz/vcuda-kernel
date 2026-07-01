// SPDX-License-Identifier: GPL-2.0-only
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include "vgpu_events.h"
#include "vgpu_stats.h"

struct vgpu_event_ring {
	spinlock_t lock;
	__u64 next_seq;
	__u32 start;
	__u32 count;
	struct vgpu_event_record records[VGPU_EVENT_RING_SIZE];
};

struct vgpu_timeslice_ring {
	spinlock_t lock;
	__u64 next_seq;
	__u32 start;
	__u32 count;
	struct vgpu_timeslice_record records[VGPU_TIMESLICE_TRACE_SIZE];
};

static struct vgpu_event_ring vgpu_events;
static struct vgpu_timeslice_ring vgpu_timeslices;

void vgpu_events_init(void)
{
	spin_lock_init(&vgpu_events.lock);
	vgpu_events.next_seq = 1;
	vgpu_events.start = 0;
	vgpu_events.count = 0;
	memset(vgpu_events.records, 0, sizeof(vgpu_events.records));
	spin_lock_init(&vgpu_timeslices.lock);
	vgpu_timeslices.next_seq = 1;
	vgpu_timeslices.start = 0;
	vgpu_timeslices.count = 0;
	memset(vgpu_timeslices.records, 0, sizeof(vgpu_timeslices.records));
}

const char *vgpu_event_type_name(__u32 type)
{
	switch (type) {
	case VGPU_EVENT_POLICY_SET:
		return "POLICY_SET";
	case VGPU_EVENT_IOCTL_SEEN:
		return "IOCTL_SEEN";
	case VGPU_EVENT_MEMORY_ALLOC:
		return "MEMORY_ALLOC";
	case VGPU_EVENT_MEMORY_FREE:
		return "MEMORY_FREE";
	case VGPU_EVENT_MEMORY_WOULD_DENY:
		return "MEMORY_WOULD_DENY";
	case VGPU_EVENT_MEMORY_DENIED:
		return "MEMORY_DENIED";
	case VGPU_EVENT_TIMESLICE_SEEN:
		return "TIMESLICE_SEEN";
	case VGPU_EVENT_TIMESLICE_WOULD_REWRITE:
		return "TIMESLICE_WOULD_REWRITE";
	case VGPU_EVENT_TIMESLICE_REWRITTEN:
		return "TIMESLICE_REWRITTEN";
	case VGPU_EVENT_HOOK_ERROR:
		return "HOOK_ERROR";
	case VGPU_EVENT_EMERGENCY_DISABLED:
		return "EMERGENCY_DISABLED";
	case VGPU_EVENT_NVIDIA_OPEN:
		return "NVIDIA_OPEN";
	case VGPU_EVENT_NVIDIA_RELEASE:
		return "NVIDIA_RELEASE";
	default:
		return "UNKNOWN";
	}
}

const char *vgpu_timeslice_reason_name(__u32 reason)
{
	switch (reason) {
	case VGPU_TIMESLICE_REASON_NONE:
		return "none";
	case VGPU_TIMESLICE_REASON_NO_POLICY:
		return "no_policy";
	case VGPU_TIMESLICE_REASON_NO_COMPUTE_FLAG:
		return "no_compute_flag";
	case VGPU_TIMESLICE_REASON_UNCHANGED:
		return "unchanged";
	case VGPU_TIMESLICE_REASON_POLICY_DRY_RUN:
		return "policy_dry_run";
	case VGPU_TIMESLICE_REASON_NOT_ENFORCING:
		return "not_enforcing";
	case VGPU_TIMESLICE_REASON_WRITE_FAILED:
		return "write_failed";
	case VGPU_TIMESLICE_REASON_CLAMPED_MIN:
		return "clamped_min";
	case VGPU_TIMESLICE_REASON_CLAMPED_MAX:
		return "clamped_max";
	default:
		return "unknown";
	}
}

void vgpu_events_push(enum vgpu_event_type type, __s32 pid, __s32 tgid,
		      __s32 gpu_minor, __u64 old_value, __u64 new_value,
		      __s32 error, __u32 flags)
{
	struct vgpu_event_record *record;
	unsigned long irq_flags;
	__u32 index;

	spin_lock_irqsave(&vgpu_events.lock, irq_flags);
	if (vgpu_events.count == VGPU_EVENT_RING_SIZE) {
		index = vgpu_events.start;
		vgpu_events.start = (vgpu_events.start + 1) % VGPU_EVENT_RING_SIZE;
		vgpu_stats_inc(VGPU_STAT_EVENTS_DROPPED);
	} else {
		index = (vgpu_events.start + vgpu_events.count) %
			VGPU_EVENT_RING_SIZE;
		vgpu_events.count++;
	}

	record = &vgpu_events.records[index];
	record->seq = vgpu_events.next_seq++;
	record->ts_ns = jiffies_to_nsecs(jiffies);
	record->type = type;
	record->pid = pid;
	record->tgid = tgid;
	record->gpu_minor = gpu_minor;
	record->old_value = old_value;
	record->new_value = new_value;
	record->error = error;
	record->flags = flags;
	spin_unlock_irqrestore(&vgpu_events.lock, irq_flags);
}

size_t vgpu_events_snapshot(struct vgpu_event_record *out, size_t max_records)
{
	unsigned long irq_flags;
	size_t copied = 0;
	__u32 i;

	if (!out || max_records == 0)
		return 0;

	spin_lock_irqsave(&vgpu_events.lock, irq_flags);
	while (copied < max_records && copied < vgpu_events.count) {
		i = (vgpu_events.start + copied) % VGPU_EVENT_RING_SIZE;
		out[copied] = vgpu_events.records[i];
		copied++;
	}
	spin_unlock_irqrestore(&vgpu_events.lock, irq_flags);

	return copied;
}

void vgpu_timeslice_trace_push(enum vgpu_event_type type, __s32 pid,
			       __s32 tgid, __s32 gpu_minor,
			       __u64 old_timeslice_us, __u64 new_timeslice_us,
			       __u64 cgroup_id, __u32 weight, __u32 policy_scope,
			       __u32 reason, __s32 error,
			       __u32 flags)
{
	struct vgpu_timeslice_record *record;
	unsigned long irq_flags;
	__u32 index;

	spin_lock_irqsave(&vgpu_timeslices.lock, irq_flags);
	if (vgpu_timeslices.count == VGPU_TIMESLICE_TRACE_SIZE) {
		index = vgpu_timeslices.start;
		vgpu_timeslices.start = (vgpu_timeslices.start + 1) %
			VGPU_TIMESLICE_TRACE_SIZE;
	} else {
		index = (vgpu_timeslices.start + vgpu_timeslices.count) %
			VGPU_TIMESLICE_TRACE_SIZE;
		vgpu_timeslices.count++;
	}

	record = &vgpu_timeslices.records[index];
	record->seq = vgpu_timeslices.next_seq++;
	record->ts_ns = jiffies_to_nsecs(jiffies);
	record->type = type;
	record->pid = pid;
	record->tgid = tgid;
	record->gpu_minor = gpu_minor;
	record->old_timeslice_us = old_timeslice_us;
	record->new_timeslice_us = new_timeslice_us;
	record->cgroup_id = cgroup_id;
	record->weight = weight;
	record->policy_scope = policy_scope;
	record->reason = reason;
	record->error = error;
	record->flags = flags;
	spin_unlock_irqrestore(&vgpu_timeslices.lock, irq_flags);
}

size_t vgpu_timeslice_trace_snapshot(struct vgpu_timeslice_record *out,
				     size_t max_records)
{
	unsigned long irq_flags;
	size_t copied = 0;
	__u32 i;

	if (!out || max_records == 0)
		return 0;

	spin_lock_irqsave(&vgpu_timeslices.lock, irq_flags);
	while (copied < max_records && copied < vgpu_timeslices.count) {
		i = (vgpu_timeslices.start + copied) % VGPU_TIMESLICE_TRACE_SIZE;
		out[copied] = vgpu_timeslices.records[i];
		copied++;
	}
	spin_unlock_irqrestore(&vgpu_timeslices.lock, irq_flags);

	return copied;
}

#ifdef CONFIG_KUNIT
void vgpu_events_reset_for_test(void)
{
	vgpu_events_init();
}
#endif
