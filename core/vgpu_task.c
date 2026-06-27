// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include "vgpu_task.h"

#define VGPU_TASK_SNAPSHOT_MAX 1024
#define VGPU_TASK_MEMORY_OBJECT_MAX 4096

struct vgpu_memory_object {
	struct list_head node;
	__s32 tgid;
	__s32 gpu_minor;
	__u32 object;
	__u64 bytes;
};

struct vgpu_task_ctx {
	struct list_head node;
	__s32 pid;
	__s32 tgid;
	__s32 gpu_minor;
	__u32 nvidia_major;
	__u32 fd_refs;
	__u64 memory_used_bytes;
	__u64 last_timeslice;
	unsigned long last_seen_jiffies;
};

static LIST_HEAD(vgpu_tasks);
static LIST_HEAD(vgpu_memory_objects);
static DEFINE_SPINLOCK(vgpu_tasks_lock);

static bool clear_memory_on_last_close = true;
module_param(clear_memory_on_last_close, bool, 0644);
MODULE_PARM_DESC(clear_memory_on_last_close,
		 "clear task memory accounting when the last NVIDIA fd closes");

static struct vgpu_task_ctx *vgpu_task_find_locked(__s32 tgid,
						   __s32 gpu_minor)
{
	struct vgpu_task_ctx *ctx;

	list_for_each_entry(ctx, &vgpu_tasks, node) {
		if (ctx->tgid == tgid && ctx->gpu_minor == gpu_minor)
			return ctx;
	}

	return NULL;
}

static struct vgpu_task_ctx *vgpu_task_get_or_create_locked(__s32 pid,
							    __s32 tgid,
							    __s32 gpu_minor,
							    __u32 nvidia_major)
{
	struct vgpu_task_ctx *ctx;

	ctx = vgpu_task_find_locked(tgid, gpu_minor);
	if (ctx)
		return ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_ATOMIC);
	if (!ctx)
		return NULL;

	ctx->pid = pid;
	ctx->tgid = tgid;
	ctx->gpu_minor = gpu_minor;
	ctx->nvidia_major = nvidia_major;
	ctx->last_seen_jiffies = jiffies;
	list_add_tail(&ctx->node, &vgpu_tasks);
	return ctx;
}

static struct vgpu_memory_object *vgpu_task_memory_object_find_locked(__s32 tgid,
							      __s32 gpu_minor,
							      __u32 object)
{
	struct vgpu_memory_object *entry;

	list_for_each_entry(entry, &vgpu_memory_objects, node) {
		if (entry->tgid == tgid && entry->gpu_minor == gpu_minor &&
		    entry->object == object)
			return entry;
	}

	return NULL;
}

static void vgpu_task_memory_objects_clear_locked(__s32 tgid, __s32 gpu_minor)
{
	struct vgpu_memory_object *entry;
	struct vgpu_memory_object *tmp;

	list_for_each_entry_safe(entry, tmp, &vgpu_memory_objects, node) {
		if ((tgid < 0 && gpu_minor < 0) ||
		    (entry->tgid == tgid && entry->gpu_minor == gpu_minor)) {
			list_del(&entry->node);
			kfree(entry);
		}
	}
}

static size_t vgpu_task_memory_object_count_locked(void)
{
	struct vgpu_memory_object *entry;
	size_t count = 0;

	list_for_each_entry(entry, &vgpu_memory_objects, node)
		count++;

	return count;
}

int vgpu_task_registry_init(void)
{
	return 0;
}

void vgpu_task_registry_exit(void)
{
	struct vgpu_task_ctx *ctx;
	struct vgpu_task_ctx *tmp;
	unsigned long flags;

	spin_lock_irqsave(&vgpu_tasks_lock, flags);
	list_for_each_entry_safe(ctx, tmp, &vgpu_tasks, node) {
		list_del(&ctx->node);
		kfree(ctx);
	}
	vgpu_task_memory_objects_clear_locked(-1, -1);
	spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
}

int vgpu_task_open(__s32 pid, __s32 tgid, __s32 gpu_minor,
		   __u32 nvidia_major)
{
	struct vgpu_task_ctx *ctx;
	unsigned long flags;

	if (tgid <= 0 || gpu_minor < 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_tasks_lock, flags);
	ctx = vgpu_task_get_or_create_locked(pid, tgid, gpu_minor,
					     nvidia_major);
	if (!ctx) {
		spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
		return -ENOMEM;
	}

	ctx->pid = pid;
	ctx->nvidia_major = nvidia_major;
	ctx->last_seen_jiffies = jiffies;
	ctx->fd_refs++;
	spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
	return 0;
}

void vgpu_task_touch(__s32 pid, __s32 tgid, __s32 gpu_minor,
		     __u32 nvidia_major)
{
	struct vgpu_task_ctx *ctx;
	unsigned long flags;

	if (tgid <= 0 || gpu_minor < 0)
		return;

	spin_lock_irqsave(&vgpu_tasks_lock, flags);
	ctx = vgpu_task_find_locked(tgid, gpu_minor);
	if (ctx) {
		ctx->pid = pid;
		ctx->nvidia_major = nvidia_major;
		ctx->last_seen_jiffies = jiffies;
	}
	spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
}

void vgpu_task_close(__s32 tgid, __s32 gpu_minor)
{
	struct vgpu_task_ctx *ctx;
	unsigned long flags;

	if (tgid <= 0 || gpu_minor < 0)
		return;

	spin_lock_irqsave(&vgpu_tasks_lock, flags);
	ctx = vgpu_task_find_locked(tgid, gpu_minor);
	if (!ctx) {
		spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
		return;
	}

	if (ctx->fd_refs > 0)
		ctx->fd_refs--;
	ctx->last_seen_jiffies = jiffies;
	if (ctx->fd_refs == 0 && clear_memory_on_last_close) {
		ctx->memory_used_bytes = 0;
		vgpu_task_memory_objects_clear_locked(tgid, gpu_minor);
	}

	if (ctx->fd_refs == 0 && ctx->memory_used_bytes == 0) {
		list_del(&ctx->node);
		kfree(ctx);
	}
	spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
}

int vgpu_task_memory_charge_object(__s32 pid, __s32 tgid, __s32 gpu_minor,
				   __u32 nvidia_major, __u32 object,
				   __u64 bytes, __u64 limit_bytes,
				   bool dry_run, bool *would_deny)
{
	struct vgpu_memory_object *entry;
	struct vgpu_task_ctx *ctx;
	__u64 old_bytes = 0;
	bool over_limit = false;
	bool overflow = false;
	unsigned long flags;
	int ret = 0;

	if (would_deny)
		*would_deny = false;
	if (tgid <= 0 || gpu_minor < 0 || object == 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_tasks_lock, flags);
	ctx = vgpu_task_get_or_create_locked(pid, tgid, gpu_minor,
					     nvidia_major);
	if (!ctx) {
		spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
		return -ENOMEM;
	}

	entry = vgpu_task_memory_object_find_locked(tgid, gpu_minor, object);
	if (entry)
		old_bytes = entry->bytes;

	if (ctx->memory_used_bytes < old_bytes) {
		ctx->memory_used_bytes = 0;
	} else {
		ctx->memory_used_bytes -= old_bytes;
	}

	if (bytes > (~(__u64)0) - ctx->memory_used_bytes) {
		overflow = true;
		over_limit = true;
	} else if (limit_bytes > 0 &&
		   ctx->memory_used_bytes + bytes > limit_bytes) {
		over_limit = true;
	}

	if (over_limit) {
		ctx->memory_used_bytes += old_bytes;
		if (would_deny)
			*would_deny = true;
		if (overflow) {
			ret = -EOVERFLOW;
			goto out;
		}
		if (!dry_run) {
			ret = -EDQUOT;
			goto out;
		}
		ctx->memory_used_bytes -= old_bytes;
		ret = 1;
	}

	if (!entry) {
		if (vgpu_task_memory_object_count_locked() >=
		    VGPU_TASK_MEMORY_OBJECT_MAX) {
			ctx->memory_used_bytes += old_bytes;
			ret = -ENOSPC;
			goto out;
		}
		entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
		if (!entry) {
			ctx->memory_used_bytes += old_bytes;
			ret = -ENOMEM;
			goto out;
		}
		entry->tgid = tgid;
		entry->gpu_minor = gpu_minor;
		entry->object = object;
		list_add_tail(&entry->node, &vgpu_memory_objects);
	}

	entry->bytes = bytes;
	ctx->pid = pid;
	ctx->nvidia_major = nvidia_major;
	ctx->memory_used_bytes += bytes;
	ctx->last_seen_jiffies = jiffies;

out:
	spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
	return ret;
}

int vgpu_task_memory_charge(__s32 pid, __s32 tgid, __s32 gpu_minor,
			    __u32 nvidia_major, __u64 bytes,
			    __u64 limit_bytes, bool dry_run,
			    bool *would_deny)
{
	struct vgpu_task_ctx *ctx;
	bool over_limit = false;
	bool overflow = false;
	unsigned long flags;
	int ret = 0;

	if (would_deny)
		*would_deny = false;
	if (tgid <= 0 || gpu_minor < 0 || bytes == 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_tasks_lock, flags);
	ctx = vgpu_task_get_or_create_locked(pid, tgid, gpu_minor,
					     nvidia_major);
	if (!ctx) {
		spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
		return -ENOMEM;
	}

	if (bytes > (~(__u64)0) - ctx->memory_used_bytes) {
		overflow = true;
		over_limit = true;
	} else if (limit_bytes > 0 &&
		   ctx->memory_used_bytes + bytes > limit_bytes) {
		over_limit = true;
	}

	if (over_limit) {
		if (would_deny)
			*would_deny = true;
		if (overflow) {
			ret = -EOVERFLOW;
			goto out;
		}
		if (!dry_run) {
			ret = -EDQUOT;
			goto out;
		}
		ret = 1;
	}

	ctx->pid = pid;
	ctx->nvidia_major = nvidia_major;
	ctx->memory_used_bytes += bytes;
	ctx->last_seen_jiffies = jiffies;

out:
	spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
	return ret;
}

int vgpu_task_memory_uncharge(__s32 tgid, __s32 gpu_minor, __u64 bytes)
{
	struct vgpu_task_ctx *ctx;
	unsigned long flags;
	int ret = 0;

	if (tgid <= 0 || gpu_minor < 0 || bytes == 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_tasks_lock, flags);
	ctx = vgpu_task_find_locked(tgid, gpu_minor);
	if (!ctx) {
		spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
		return -ENOENT;
	}

	if (bytes > ctx->memory_used_bytes) {
		ctx->memory_used_bytes = 0;
		ret = -ERANGE;
	} else {
		ctx->memory_used_bytes -= bytes;
	}
	ctx->last_seen_jiffies = jiffies;
	spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
	return ret;
}

int vgpu_task_memory_uncharge_object(__s32 tgid, __s32 gpu_minor,
				     __u32 object, __u64 *bytes_out)
{
	struct vgpu_memory_object *entry;
	struct vgpu_task_ctx *ctx;
	unsigned long flags;
	__u64 bytes;
	int ret = 0;

	if (bytes_out)
		*bytes_out = 0;
	if (tgid <= 0 || gpu_minor < 0 || object == 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_tasks_lock, flags);
	entry = vgpu_task_memory_object_find_locked(tgid, gpu_minor, object);
	if (!entry) {
		spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
		return -ENOENT;
	}
	ctx = vgpu_task_find_locked(tgid, gpu_minor);
	if (!ctx) {
		spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
		return -ENOENT;
	}

	bytes = entry->bytes;
	list_del(&entry->node);
	kfree(entry);
	if (bytes > ctx->memory_used_bytes) {
		ctx->memory_used_bytes = 0;
		ret = -ERANGE;
	} else {
		ctx->memory_used_bytes -= bytes;
	}
	ctx->last_seen_jiffies = jiffies;
	spin_unlock_irqrestore(&vgpu_tasks_lock, flags);

	if (bytes_out)
		*bytes_out = bytes;
	return ret;
}

int vgpu_task_timeslice_update(__s32 pid, __s32 tgid, __s32 gpu_minor,
			       __u32 nvidia_major, __u64 timeslice)
{
	struct vgpu_task_ctx *ctx;
	unsigned long flags;

	if (tgid <= 0 || gpu_minor < 0 || timeslice == 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_tasks_lock, flags);
	ctx = vgpu_task_get_or_create_locked(pid, tgid, gpu_minor, nvidia_major);
	if (!ctx) {
		spin_unlock_irqrestore(&vgpu_tasks_lock, flags);
		return -ENOMEM;
	}

	ctx->pid = pid;
	ctx->nvidia_major = nvidia_major;
	ctx->last_timeslice = timeslice;
	ctx->last_seen_jiffies = jiffies;
	spin_unlock_irqrestore(&vgpu_tasks_lock, flags);

	return 0;
}

int vgpu_task_for_each(int (*fn)(const struct vgpu_task_snapshot *task,
				 void *data),
		       void *data)
{
	struct vgpu_task_ctx *ctx;
	struct vgpu_task_snapshot *snapshots;
	unsigned long flags;
	size_t count = 0;
	size_t i;
	int ret = 0;

	if (!fn)
		return -EINVAL;

	snapshots = kcalloc(VGPU_TASK_SNAPSHOT_MAX, sizeof(*snapshots),
			    GFP_KERNEL);
	if (!snapshots)
		return -ENOMEM;

	spin_lock_irqsave(&vgpu_tasks_lock, flags);
	list_for_each_entry(ctx, &vgpu_tasks, node) {
		if (count == VGPU_TASK_SNAPSHOT_MAX)
			break;
		snapshots[count].pid = ctx->pid;
		snapshots[count].tgid = ctx->tgid;
		snapshots[count].gpu_minor = ctx->gpu_minor;
		snapshots[count].nvidia_major = ctx->nvidia_major;
		snapshots[count].fd_refs = ctx->fd_refs;
		snapshots[count].memory_used_bytes = ctx->memory_used_bytes;
		snapshots[count].last_timeslice = ctx->last_timeslice;
		snapshots[count].last_seen_jiffies = ctx->last_seen_jiffies;
		count++;
	}
	spin_unlock_irqrestore(&vgpu_tasks_lock, flags);

	for (i = 0; i < count; i++) {
		ret = fn(&snapshots[i], data);
		if (ret)
			break;
	}

	kfree(snapshots);
	return ret;
}

#ifdef CONFIG_KUNIT
void vgpu_task_reset_for_test(void)
{
	vgpu_task_registry_exit();
}
#endif
