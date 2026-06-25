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

static struct vgpu_event_ring vgpu_events;

void vgpu_events_init(void)
{
	spin_lock_init(&vgpu_events.lock);
	vgpu_events.next_seq = 1;
	vgpu_events.start = 0;
	vgpu_events.count = 0;
	memset(vgpu_events.records, 0, sizeof(vgpu_events.records));
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

#ifdef CONFIG_KUNIT
void vgpu_events_reset_for_test(void)
{
	vgpu_events_init();
}
#endif
