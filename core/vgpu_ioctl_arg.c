// SPDX-License-Identifier: GPL-2.0
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "vgpu_ioctl_arg.h"

struct vgpu_ioctl_arg_entry {
	bool used;
	struct vgpu_ioctl_arg_snapshot snapshot;
};

struct vgpu_ioctl_arg_value_entry {
	bool used;
	struct vgpu_ioctl_arg_value_snapshot snapshot;
};

struct vgpu_ioctl_arg_pair_entry {
	bool used;
	struct vgpu_ioctl_arg_pair_snapshot snapshot;
};

struct vgpu_ioctl_arg_detail_entry {
	bool used;
	struct vgpu_ioctl_arg_detail_snapshot snapshot;
};

static DEFINE_SPINLOCK(vgpu_ioctl_arg_lock);
static struct vgpu_ioctl_arg_entry vgpu_ioctl_arg_entries[VGPU_IOCTL_ARG_MAX];
static struct vgpu_ioctl_arg_value_entry vgpu_ioctl_arg_value_entries[VGPU_IOCTL_ARG_VALUE_MAX];
static struct vgpu_ioctl_arg_pair_entry vgpu_ioctl_arg_pair_entries[VGPU_IOCTL_ARG_PAIR_MAX];
static struct vgpu_ioctl_arg_detail_entry vgpu_ioctl_arg_detail_entries[VGPU_IOCTL_ARG_DETAIL_MAX];

unsigned long vgpu_ioctl_arg_filter_value;
module_param_named(ioctl_arg_filter_value, vgpu_ioctl_arg_filter_value,
		   ulong, 0644);
MODULE_PARM_DESC(ioctl_arg_filter_value, "show and count ioctl_arg rows containing this u32/u64 value; 0 disables");

unsigned int vgpu_ioctl_arg_value_min_count = 2;
module_param_named(ioctl_arg_value_min_count,
		   vgpu_ioctl_arg_value_min_count, uint, 0644);
MODULE_PARM_DESC(ioctl_arg_value_min_count, "minimum count shown in ioctl_arg_values; 0 shows all recorded values");

void vgpu_ioctl_arg_init(void)
{
	unsigned long flags;

	spin_lock_irqsave(&vgpu_ioctl_arg_lock, flags);
	memset(vgpu_ioctl_arg_entries, 0, sizeof(vgpu_ioctl_arg_entries));
	memset(vgpu_ioctl_arg_value_entries, 0,
	       sizeof(vgpu_ioctl_arg_value_entries));
	memset(vgpu_ioctl_arg_pair_entries, 0,
	       sizeof(vgpu_ioctl_arg_pair_entries));
	memset(vgpu_ioctl_arg_detail_entries, 0,
	       sizeof(vgpu_ioctl_arg_detail_entries));
	spin_unlock_irqrestore(&vgpu_ioctl_arg_lock, flags);
}

static void vgpu_ioctl_arg_record_value_locked(__u32 cmd, __u32 offset,
					       __u32 value)
{
	struct vgpu_ioctl_arg_value_entry *free_entry = NULL;
	struct vgpu_ioctl_arg_value_entry *entry;
	size_t i;

	for (i = 0; i < VGPU_IOCTL_ARG_VALUE_MAX; i++) {
		entry = &vgpu_ioctl_arg_value_entries[i];
		if (!entry->used) {
			if (!free_entry)
				free_entry = entry;
			continue;
		}
		if (entry->snapshot.cmd == cmd &&
		    entry->snapshot.offset == offset &&
		    entry->snapshot.value == value)
			goto update;
	}

	entry = free_entry;
	if (!entry)
		return;
	entry->used = true;
	entry->snapshot.cmd = cmd;
	entry->snapshot.offset = offset;
	entry->snapshot.value = value;
	entry->snapshot.count = 0;

update:
	entry->snapshot.count++;
}

void vgpu_ioctl_arg_exit(void)
{
	vgpu_ioctl_arg_init();
}

void vgpu_ioctl_arg_record_pair(__u32 cmd, __u32 class_value,
				__u64 size_value)
{
	struct vgpu_ioctl_arg_pair_entry *free_entry = NULL;
	struct vgpu_ioctl_arg_pair_entry *entry;
	unsigned long flags;
	size_t i;

	spin_lock_irqsave(&vgpu_ioctl_arg_lock, flags);
	for (i = 0; i < VGPU_IOCTL_ARG_PAIR_MAX; i++) {
		entry = &vgpu_ioctl_arg_pair_entries[i];
		if (!entry->used) {
			if (!free_entry)
				free_entry = entry;
			continue;
		}
		if (entry->snapshot.cmd == cmd &&
		    entry->snapshot.class_value == class_value &&
		    entry->snapshot.size_value == size_value)
			goto update;
	}

	entry = free_entry;
	if (!entry)
		goto out;
	entry->used = true;
	entry->snapshot.cmd = cmd;
	entry->snapshot.class_value = class_value;
	entry->snapshot.size_value = size_value;
	entry->snapshot.count = 0;

update:
	entry->snapshot.count++;
out:
	spin_unlock_irqrestore(&vgpu_ioctl_arg_lock, flags);
}

static bool vgpu_ioctl_arg_detail_same(const struct vgpu_ioctl_arg_detail_snapshot *a,
					       const struct vgpu_ioctl_arg_detail_snapshot *b)
{
	return a->cmd == b->cmd &&
	       a->parent == b->parent &&
	       a->object == b->object &&
	       a->class_value == b->class_value &&
	       a->owner == b->owner &&
	       a->type == b->type &&
	       a->flags == b->flags &&
	       a->attr == b->attr &&
	       a->attr2 == b->attr2 &&
	       a->tag == b->tag &&
	       a->size_value == b->size_value &&
	       a->offset == b->offset &&
	       a->limit == b->limit &&
	       a->address == b->address;
}

void vgpu_ioctl_arg_record_detail(__u32 cmd,
				  const struct vgpu_ioctl_arg_detail_snapshot *detail)
{
	struct vgpu_ioctl_arg_detail_entry *free_entry = NULL;
	struct vgpu_ioctl_arg_detail_entry *entry;
	struct vgpu_ioctl_arg_detail_snapshot key;
	unsigned long flags;
	size_t i;

	if (!detail)
		return;

	key = *detail;
	key.cmd = cmd;
	key.count = 0;

	spin_lock_irqsave(&vgpu_ioctl_arg_lock, flags);
	for (i = 0; i < VGPU_IOCTL_ARG_DETAIL_MAX; i++) {
		entry = &vgpu_ioctl_arg_detail_entries[i];
		if (!entry->used) {
			if (!free_entry)
				free_entry = entry;
			continue;
		}
		if (vgpu_ioctl_arg_detail_same(&entry->snapshot, &key))
			goto update;
	}

	entry = free_entry;
	if (!entry)
		goto out;
	entry->used = true;
	entry->snapshot = key;

update:
	entry->snapshot.count++;
out:
	spin_unlock_irqrestore(&vgpu_ioctl_arg_lock, flags);
}

void vgpu_ioctl_arg_record(__u32 cmd, const void *bytes, size_t len)
{
	const u8 *data = bytes;
	struct vgpu_ioctl_arg_entry *free_entry;
	struct vgpu_ioctl_arg_entry *entry;
	__u32 value32;
	__u64 value64;
	__u32 offset;
	unsigned long flags;
	size_t i;

	if (!bytes)
		return;

	spin_lock_irqsave(&vgpu_ioctl_arg_lock, flags);
	for (offset = 0; offset + sizeof(value64) <= len &&
	     offset < VGPU_IOCTL_ARG_BYTES; offset += sizeof(value32)) {
		memcpy(&value32, data + offset, sizeof(value32));
		memcpy(&value64, data + offset, sizeof(value64));
		vgpu_ioctl_arg_record_value_locked(cmd, offset, value32);
		free_entry = NULL;

		for (i = 0; i < VGPU_IOCTL_ARG_MAX; i++) {
			entry = &vgpu_ioctl_arg_entries[i];
			if (!entry->used) {
				if (!free_entry)
					free_entry = entry;
				continue;
			}
			if (entry->snapshot.cmd == cmd &&
			    entry->snapshot.offset == offset)
				goto update;
		}

		entry = free_entry;
		if (!entry)
			continue;
		entry->used = true;
		entry->snapshot.cmd = cmd;
		entry->snapshot.offset = offset;
		entry->snapshot.count = 0;
		entry->snapshot.min_u32 = value32;
		entry->snapshot.max_u32 = value32;
		entry->snapshot.min_u64 = value64;
		entry->snapshot.max_u64 = value64;

update:
		entry->snapshot.count++;
		if (vgpu_ioctl_arg_filter_value != 0) {
			if (value32 == vgpu_ioctl_arg_filter_value)
				entry->snapshot.filter_u32_hits++;
			if (value64 == vgpu_ioctl_arg_filter_value)
				entry->snapshot.filter_u64_hits++;
		}
		entry->snapshot.last_u32 = value32;
		entry->snapshot.last_u64 = value64;
		if (value32 < entry->snapshot.min_u32)
			entry->snapshot.min_u32 = value32;
		if (value32 > entry->snapshot.max_u32)
			entry->snapshot.max_u32 = value32;
		if (value64 < entry->snapshot.min_u64)
			entry->snapshot.min_u64 = value64;
		if (value64 > entry->snapshot.max_u64)
			entry->snapshot.max_u64 = value64;
	}
	spin_unlock_irqrestore(&vgpu_ioctl_arg_lock, flags);
}

void vgpu_ioctl_arg_sample_user(__u32 cmd, unsigned long user_ptr, size_t len)
{
	u8 bytes[VGPU_IOCTL_ARG_BYTES];
	size_t copy_len = min_t(size_t, len, sizeof(bytes));

	if (!user_ptr || copy_len == 0)
		return;
	if (copy_from_user_nofault(bytes, (const void __user *)user_ptr,
				   copy_len))
		return;

	vgpu_ioctl_arg_record(cmd, bytes, copy_len);
}

void vgpu_ioctl_arg_sample_user_ptr(__u32 cmd, unsigned long user_ptr,
				   size_t len, __u32 ptr_offset,
				   size_t nested_len)
{
	unsigned long nested_ptr;
	unsigned long ptr_addr;

	if (!user_ptr || ptr_offset + sizeof(nested_ptr) > len)
		return;
	if (user_ptr > ULONG_MAX - ptr_offset)
		return;

	ptr_addr = user_ptr + ptr_offset;
	if (copy_from_user_nofault(&nested_ptr, (const void __user *)ptr_addr,
				   sizeof(nested_ptr)))
		return;

	vgpu_ioctl_arg_sample_user(cmd, nested_ptr, nested_len);
}

bool vgpu_ioctl_arg_cmd_match(__u32 cmd, const __u32 *cmds, size_t count)
{
	size_t i;

	if (!cmds)
		return false;

	for (i = 0; i < count; i++) {
		if (cmds[i] == cmd)
			return true;
	}

	return false;
}

size_t vgpu_ioctl_arg_snapshot(struct vgpu_ioctl_arg_snapshot *out,
			       size_t max_records)
{
	unsigned long flags;
	size_t copied = 0;
	size_t i;

	if (!out || max_records == 0)
		return 0;

	spin_lock_irqsave(&vgpu_ioctl_arg_lock, flags);
	for (i = 0; i < VGPU_IOCTL_ARG_MAX && copied < max_records; i++) {
		if (!vgpu_ioctl_arg_entries[i].used)
			continue;
		out[copied] = vgpu_ioctl_arg_entries[i].snapshot;
		copied++;
	}
	spin_unlock_irqrestore(&vgpu_ioctl_arg_lock, flags);

	return copied;
}

size_t vgpu_ioctl_arg_value_snapshot(struct vgpu_ioctl_arg_value_snapshot *out,
				     size_t max_records)
{
	unsigned long flags;
	size_t copied = 0;
	size_t i;

	if (!out || max_records == 0)
		return 0;

	spin_lock_irqsave(&vgpu_ioctl_arg_lock, flags);
	for (i = 0; i < VGPU_IOCTL_ARG_VALUE_MAX && copied < max_records; i++) {
		if (!vgpu_ioctl_arg_value_entries[i].used)
			continue;
		out[copied] = vgpu_ioctl_arg_value_entries[i].snapshot;
		copied++;
	}
	spin_unlock_irqrestore(&vgpu_ioctl_arg_lock, flags);

	return copied;
}

size_t vgpu_ioctl_arg_pair_snapshot(struct vgpu_ioctl_arg_pair_snapshot *out,
				    size_t max_records)
{
	unsigned long flags;
	size_t copied = 0;
	size_t i;

	if (!out || max_records == 0)
		return 0;

	spin_lock_irqsave(&vgpu_ioctl_arg_lock, flags);
	for (i = 0; i < VGPU_IOCTL_ARG_PAIR_MAX && copied < max_records; i++) {
		if (!vgpu_ioctl_arg_pair_entries[i].used)
			continue;
		out[copied] = vgpu_ioctl_arg_pair_entries[i].snapshot;
		copied++;
	}
	spin_unlock_irqrestore(&vgpu_ioctl_arg_lock, flags);

	return copied;
}

size_t vgpu_ioctl_arg_detail_snapshot(struct vgpu_ioctl_arg_detail_snapshot *out,
				      size_t max_records)
{
	unsigned long flags;
	size_t copied = 0;
	size_t i;

	if (!out || max_records == 0)
		return 0;

	spin_lock_irqsave(&vgpu_ioctl_arg_lock, flags);
	for (i = 0; i < VGPU_IOCTL_ARG_DETAIL_MAX && copied < max_records; i++) {
		if (!vgpu_ioctl_arg_detail_entries[i].used)
			continue;
		out[copied] = vgpu_ioctl_arg_detail_entries[i].snapshot;
		copied++;
	}
	spin_unlock_irqrestore(&vgpu_ioctl_arg_lock, flags);

	return copied;
}

#ifdef CONFIG_KUNIT
void vgpu_ioctl_arg_reset_for_test(void)
{
	vgpu_ioctl_arg_exit();
}
#endif
