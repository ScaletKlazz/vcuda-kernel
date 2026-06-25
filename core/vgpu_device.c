// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include "vgpu_device.h"

struct vgpu_device_file_entry {
	struct list_head node;
	struct vgpu_device_file file;
};

static LIST_HEAD(vgpu_device_files);
static DEFINE_SPINLOCK(vgpu_device_files_lock);

static struct vgpu_device_file_entry *vgpu_device_find_locked(struct file *file)
{
	struct vgpu_device_file_entry *entry;

	list_for_each_entry(entry, &vgpu_device_files, node) {
		if (entry->file.file == file)
			return entry;
	}

	return NULL;
}

int vgpu_device_registry_init(void)
{
	return 0;
}

void vgpu_device_registry_exit(void)
{
	struct vgpu_device_file_entry *entry;
	struct vgpu_device_file_entry *tmp;
	unsigned long flags;

	spin_lock_irqsave(&vgpu_device_files_lock, flags);
	list_for_each_entry_safe(entry, tmp, &vgpu_device_files, node) {
		list_del(&entry->node);
		kfree(entry);
	}
	spin_unlock_irqrestore(&vgpu_device_files_lock, flags);
}

int vgpu_device_open_file(struct file *file, __s32 pid, __s32 tgid,
			  __s32 gpu_minor, __u32 nvidia_major)
{
	struct vgpu_device_file_entry *entry;
	unsigned long flags;

	if (!file || tgid <= 0 || gpu_minor < 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_device_files_lock, flags);
	entry = vgpu_device_find_locked(file);
	if (entry) {
		entry->file.pid = pid;
		entry->file.tgid = tgid;
		entry->file.gpu_minor = gpu_minor;
		entry->file.nvidia_major = nvidia_major;
		spin_unlock_irqrestore(&vgpu_device_files_lock, flags);
		return 0;
	}

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry) {
		spin_unlock_irqrestore(&vgpu_device_files_lock, flags);
		return -ENOMEM;
	}

	entry->file.file = file;
	entry->file.pid = pid;
	entry->file.tgid = tgid;
	entry->file.gpu_minor = gpu_minor;
	entry->file.nvidia_major = nvidia_major;
	list_add_tail(&entry->node, &vgpu_device_files);
	spin_unlock_irqrestore(&vgpu_device_files_lock, flags);
	return 1;
}

bool vgpu_device_lookup_file(struct file *file, struct vgpu_device_file *out)
{
	struct vgpu_device_file_entry *entry;
	bool found = false;
	unsigned long flags;

	if (!file || !out)
		return false;

	spin_lock_irqsave(&vgpu_device_files_lock, flags);
	entry = vgpu_device_find_locked(file);
	if (entry) {
		*out = entry->file;
		found = true;
	}
	spin_unlock_irqrestore(&vgpu_device_files_lock, flags);

	return found;
}

bool vgpu_device_close_file(struct file *file, struct vgpu_device_file *out)
{
	struct vgpu_device_file_entry *entry;
	bool found = false;
	unsigned long flags;

	if (!file || !out)
		return false;

	spin_lock_irqsave(&vgpu_device_files_lock, flags);
	entry = vgpu_device_find_locked(file);
	if (entry) {
		*out = entry->file;
		list_del(&entry->node);
		kfree(entry);
		found = true;
	}
	spin_unlock_irqrestore(&vgpu_device_files_lock, flags);

	return found;
}
