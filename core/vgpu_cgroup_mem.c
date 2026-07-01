// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "vgpu_cgroup_mem.h"

#define VGPU_CGROUP_MEM_SNAPSHOT_MAX 1024
#define VGPU_CGROUP_MEM_OBJECT_MAX 4096

struct vgpu_cgroup_mem_ctx {
	struct list_head node;
	__u64 cgroup_id;
	__s32 gpu_minor;
	__u64 memory_used_bytes;
	__u64 alloc_seen;
	__u64 free_seen;
	__u64 would_deny;
	__u64 denied;
};

struct vgpu_cgroup_mem_object {
	struct list_head node;
	__u64 cgroup_id;
	__s32 gpu_minor;
	__u32 object;
	__u64 bytes;
};

static LIST_HEAD(vgpu_cgroup_mems);
static LIST_HEAD(vgpu_cgroup_mem_objects);
static DEFINE_SPINLOCK(vgpu_cgroup_mem_lock);

static struct vgpu_cgroup_mem_ctx *vgpu_cgroup_mem_find_locked(
	__u64 cgroup_id, __s32 gpu_minor)
{
	struct vgpu_cgroup_mem_ctx *ctx;

	list_for_each_entry(ctx, &vgpu_cgroup_mems, node) {
		if (ctx->cgroup_id == cgroup_id && ctx->gpu_minor == gpu_minor)
			return ctx;
	}

	return NULL;
}

static struct vgpu_cgroup_mem_ctx *vgpu_cgroup_mem_get_or_create_locked(
	__u64 cgroup_id, __s32 gpu_minor)
{
	struct vgpu_cgroup_mem_ctx *ctx;

	ctx = vgpu_cgroup_mem_find_locked(cgroup_id, gpu_minor);
	if (ctx)
		return ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_ATOMIC);
	if (!ctx)
		return NULL;

	ctx->cgroup_id = cgroup_id;
	ctx->gpu_minor = gpu_minor;
	list_add_tail(&ctx->node, &vgpu_cgroup_mems);
	return ctx;
}

static struct vgpu_cgroup_mem_object *vgpu_cgroup_mem_object_find_locked(
	__u64 cgroup_id, __s32 gpu_minor, __u32 object)
{
	struct vgpu_cgroup_mem_object *entry;

	list_for_each_entry(entry, &vgpu_cgroup_mem_objects, node) {
		if (entry->cgroup_id == cgroup_id &&
		    entry->gpu_minor == gpu_minor && entry->object == object)
			return entry;
	}

	return NULL;
}

static size_t vgpu_cgroup_mem_object_count_locked(void)
{
	struct vgpu_cgroup_mem_object *entry;
	size_t count = 0;

	list_for_each_entry(entry, &vgpu_cgroup_mem_objects, node)
		count++;

	return count;
}

static void vgpu_cgroup_mem_clear_locked(void)
{
	struct vgpu_cgroup_mem_ctx *ctx;
	struct vgpu_cgroup_mem_ctx *ctx_tmp;
	struct vgpu_cgroup_mem_object *object;
	struct vgpu_cgroup_mem_object *object_tmp;

	list_for_each_entry_safe(ctx, ctx_tmp, &vgpu_cgroup_mems, node) {
		list_del(&ctx->node);
		kfree(ctx);
	}
	list_for_each_entry_safe(object, object_tmp, &vgpu_cgroup_mem_objects,
			     node) {
		list_del(&object->node);
		kfree(object);
	}
}

int vgpu_cgroup_mem_init(void)
{
	return 0;
}

void vgpu_cgroup_mem_exit(void)
{
	unsigned long flags;

	spin_lock_irqsave(&vgpu_cgroup_mem_lock, flags);
	vgpu_cgroup_mem_clear_locked();
	spin_unlock_irqrestore(&vgpu_cgroup_mem_lock, flags);
}

int vgpu_cgroup_mem_charge(__u64 cgroup_id, __s32 gpu_minor, __u64 bytes,
				   __u64 limit_bytes, bool dry_run, bool *would_deny)
{
	struct vgpu_cgroup_mem_ctx *ctx;
	bool over_limit = false;
	bool overflow = false;
	unsigned long flags;
	int ret = 0;

	if (would_deny)
		*would_deny = false;
	if (cgroup_id == 0 || gpu_minor < 0 || bytes == 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_cgroup_mem_lock, flags);
	ctx = vgpu_cgroup_mem_get_or_create_locked(cgroup_id, gpu_minor);
	if (!ctx) {
		spin_unlock_irqrestore(&vgpu_cgroup_mem_lock, flags);
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
		ctx->would_deny++;
		if (would_deny)
			*would_deny = true;
		if (overflow) {
			ret = -EOVERFLOW;
			goto out;
		}
		if (!dry_run) {
			ctx->denied++;
			ret = -EDQUOT;
			goto out;
		}
		ret = 1;
	}

	ctx->memory_used_bytes += bytes;
	ctx->alloc_seen++;

out:
	spin_unlock_irqrestore(&vgpu_cgroup_mem_lock, flags);
	return ret;
}

int vgpu_cgroup_mem_charge_object(__u64 cgroup_id, __s32 gpu_minor,
					  __u32 object, __u64 bytes,
					  __u64 limit_bytes, bool dry_run,
					  bool *would_deny)
{
	struct vgpu_cgroup_mem_object *entry;
	struct vgpu_cgroup_mem_ctx *ctx;
	__u64 old_bytes = 0;
	bool over_limit = false;
	bool overflow = false;
	unsigned long flags;
	int ret = 0;

	if (would_deny)
		*would_deny = false;
	if (cgroup_id == 0 || gpu_minor < 0 || object == 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_cgroup_mem_lock, flags);
	ctx = vgpu_cgroup_mem_get_or_create_locked(cgroup_id, gpu_minor);
	if (!ctx) {
		spin_unlock_irqrestore(&vgpu_cgroup_mem_lock, flags);
		return -ENOMEM;
	}

	entry = vgpu_cgroup_mem_object_find_locked(cgroup_id, gpu_minor, object);
	if (entry)
		old_bytes = entry->bytes;
	if (ctx->memory_used_bytes < old_bytes)
		ctx->memory_used_bytes = 0;
	else
		ctx->memory_used_bytes -= old_bytes;

	if (bytes > (~(__u64)0) - ctx->memory_used_bytes) {
		overflow = true;
		over_limit = true;
	} else if (limit_bytes > 0 &&
		   ctx->memory_used_bytes + bytes > limit_bytes) {
		over_limit = true;
	}

	if (over_limit) {
		ctx->memory_used_bytes += old_bytes;
		ctx->would_deny++;
		if (would_deny)
			*would_deny = true;
		if (overflow) {
			ret = -EOVERFLOW;
			goto out;
		}
		if (!dry_run) {
			ctx->denied++;
			ret = -EDQUOT;
			goto out;
		}
		ctx->memory_used_bytes -= old_bytes;
		ret = 1;
	}

	if (!entry) {
		if (vgpu_cgroup_mem_object_count_locked() >=
		    VGPU_CGROUP_MEM_OBJECT_MAX) {
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
		entry->cgroup_id = cgroup_id;
		entry->gpu_minor = gpu_minor;
		entry->object = object;
		list_add_tail(&entry->node, &vgpu_cgroup_mem_objects);
	}

	entry->bytes = bytes;
	ctx->memory_used_bytes += bytes;
	ctx->alloc_seen++;

out:
	spin_unlock_irqrestore(&vgpu_cgroup_mem_lock, flags);
	return ret;
}

int vgpu_cgroup_mem_uncharge(__u64 cgroup_id, __s32 gpu_minor, __u64 bytes)
{
	struct vgpu_cgroup_mem_ctx *ctx;
	unsigned long flags;
	int ret = 0;

	if (cgroup_id == 0 || gpu_minor < 0 || bytes == 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_cgroup_mem_lock, flags);
	ctx = vgpu_cgroup_mem_find_locked(cgroup_id, gpu_minor);
	if (!ctx) {
		spin_unlock_irqrestore(&vgpu_cgroup_mem_lock, flags);
		return -ENOENT;
	}

	if (bytes > ctx->memory_used_bytes) {
		ctx->memory_used_bytes = 0;
		ret = -ERANGE;
	} else {
		ctx->memory_used_bytes -= bytes;
	}
	ctx->free_seen++;
	spin_unlock_irqrestore(&vgpu_cgroup_mem_lock, flags);
	return ret;
}

int vgpu_cgroup_mem_uncharge_object(__u64 cgroup_id, __s32 gpu_minor,
					    __u32 object, __u64 *bytes_out)
{
	struct vgpu_cgroup_mem_object *entry;
	struct vgpu_cgroup_mem_ctx *ctx;
	unsigned long flags;
	__u64 bytes;
	int ret = 0;

	if (bytes_out)
		*bytes_out = 0;
	if (cgroup_id == 0 || gpu_minor < 0 || object == 0)
		return -EINVAL;

	spin_lock_irqsave(&vgpu_cgroup_mem_lock, flags);
	entry = vgpu_cgroup_mem_object_find_locked(cgroup_id, gpu_minor,
						      object);
	if (!entry) {
		ret = -ENOENT;
		goto out;
	}
	ctx = vgpu_cgroup_mem_find_locked(cgroup_id, gpu_minor);
	if (!ctx) {
		ret = -ENOENT;
		goto out;
	}

	bytes = entry->bytes;
	list_del(&entry->node);
	kfree(entry);
	if (bytes > ctx->memory_used_bytes)
		ctx->memory_used_bytes = 0;
	else
		ctx->memory_used_bytes -= bytes;
	ctx->free_seen++;
	if (bytes_out)
		*bytes_out = bytes;

out:
	spin_unlock_irqrestore(&vgpu_cgroup_mem_lock, flags);
	return ret;
}

int vgpu_cgroup_mem_for_each(
	int (*fn)(const struct vgpu_cgroup_mem_snapshot *snapshot, void *data),
	void *data)
{
	struct vgpu_cgroup_mem_ctx *ctx;
	struct vgpu_cgroup_mem_snapshot *snapshots;
	unsigned long flags;
	size_t count = 0;
	size_t i;
	int ret = 0;

	if (!fn)
		return -EINVAL;

	snapshots = kcalloc(VGPU_CGROUP_MEM_SNAPSHOT_MAX, sizeof(*snapshots),
			    GFP_KERNEL);
	if (!snapshots)
		return -ENOMEM;

	spin_lock_irqsave(&vgpu_cgroup_mem_lock, flags);
	list_for_each_entry(ctx, &vgpu_cgroup_mems, node) {
		if (count == VGPU_CGROUP_MEM_SNAPSHOT_MAX)
			break;
		snapshots[count].cgroup_id = ctx->cgroup_id;
		snapshots[count].gpu_minor = ctx->gpu_minor;
		snapshots[count].memory_used_bytes = ctx->memory_used_bytes;
		snapshots[count].alloc_seen = ctx->alloc_seen;
		snapshots[count].free_seen = ctx->free_seen;
		snapshots[count].would_deny = ctx->would_deny;
		snapshots[count].denied = ctx->denied;
		count++;
	}
	spin_unlock_irqrestore(&vgpu_cgroup_mem_lock, flags);

	for (i = 0; i < count; i++) {
		ret = fn(&snapshots[i], data);
		if (ret)
			break;
	}

	kfree(snapshots);
	return ret;
}

#ifdef CONFIG_KUNIT
void vgpu_cgroup_mem_reset_for_test(void)
{
	vgpu_cgroup_mem_exit();
}
#endif
