// SPDX-License-Identifier: GPL-2.0-only
#include <linux/spinlock.h>
#include <linux/string.h>

#include "vgpu_ioctl_trace.h"

struct vgpu_ioctl_trace_entry {
	bool used;
	struct vgpu_ioctl_trace_snapshot snapshot;
};

static DEFINE_SPINLOCK(vgpu_ioctl_trace_lock);
static struct vgpu_ioctl_trace_entry
	vgpu_ioctl_trace_entries[VGPU_IOCTL_TRACE_MAX];

void vgpu_ioctl_trace_init(void)
{
	unsigned long flags;

	spin_lock_irqsave(&vgpu_ioctl_trace_lock, flags);
	memset(vgpu_ioctl_trace_entries, 0, sizeof(vgpu_ioctl_trace_entries));
	spin_unlock_irqrestore(&vgpu_ioctl_trace_lock, flags);
}

void vgpu_ioctl_trace_record(__u32 cmd, __s32 pid, __s32 tgid,
			     __s32 gpu_minor, __u32 nvidia_major,
			     __u64 arg)
{
	struct vgpu_ioctl_trace_entry *free_entry = NULL;
	struct vgpu_ioctl_trace_entry *entry;
	unsigned long flags;
	size_t i;

	spin_lock_irqsave(&vgpu_ioctl_trace_lock, flags);
	for (i = 0; i < VGPU_IOCTL_TRACE_MAX; i++) {
		entry = &vgpu_ioctl_trace_entries[i];
		if (!entry->used) {
			if (!free_entry)
				free_entry = entry;
			continue;
		}
		if (entry->snapshot.cmd == cmd)
			goto update;
	}

	entry = free_entry;
	if (!entry)
		goto out;

	entry->used = true;
	entry->snapshot.cmd = cmd;
	entry->snapshot.count = 0;

update:
	entry->snapshot.count++;
	entry->snapshot.last_arg = arg;
	entry->snapshot.last_pid = pid;
	entry->snapshot.last_tgid = tgid;
	entry->snapshot.last_gpu_minor = gpu_minor;
	entry->snapshot.last_nvidia_major = nvidia_major;

out:
	spin_unlock_irqrestore(&vgpu_ioctl_trace_lock, flags);
}

size_t vgpu_ioctl_trace_snapshot(struct vgpu_ioctl_trace_snapshot *out,
				 size_t max_records)
{
	unsigned long flags;
	size_t copied = 0;
	size_t i;

	if (!out || max_records == 0)
		return 0;

	spin_lock_irqsave(&vgpu_ioctl_trace_lock, flags);
	for (i = 0; i < VGPU_IOCTL_TRACE_MAX && copied < max_records; i++) {
		if (!vgpu_ioctl_trace_entries[i].used)
			continue;
		out[copied] = vgpu_ioctl_trace_entries[i].snapshot;
		copied++;
	}
	spin_unlock_irqrestore(&vgpu_ioctl_trace_lock, flags);

	return copied;
}

#ifdef CONFIG_KUNIT
void vgpu_ioctl_trace_reset_for_test(void)
{
	vgpu_ioctl_trace_init();
}
#endif
