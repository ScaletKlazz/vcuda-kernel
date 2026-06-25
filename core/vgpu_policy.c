// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/stddef.h>

#include "vgpu_policy.h"

#define VGPU_POLICY_VALID_FLAGS \
	(VGPU_POLICY_F_MEMORY | VGPU_POLICY_F_COMPUTE | VGPU_POLICY_F_DRY_RUN)

static const struct rhashtable_params vgpu_policy_rhashtable_params = {
	.key_len = sizeof(struct vgpu_policy_key),
	.key_offset = offsetof(struct vgpu_policy_entry, key),
	.head_offset = offsetof(struct vgpu_policy_entry, node),
	.automatic_shrinking = true,
};

static void vgpu_policy_entry_free(void *ptr, void *arg)
{
	struct vgpu_policy_entry *entry = ptr;

	(void)arg;
	kfree(entry);
}

int vgpu_policy_table_init(struct vgpu_policy_table *policies)
{
	int ret;

	if (!policies)
		return -EINVAL;

	mutex_init(&policies->lock);
	ret = rhashtable_init(&policies->table,
			       &vgpu_policy_rhashtable_params);
	if (ret)
		mutex_destroy(&policies->lock);

	return ret;
}

void vgpu_policy_table_destroy(struct vgpu_policy_table *policies)
{
	if (!policies)
		return;

	rhashtable_free_and_destroy(&policies->table, vgpu_policy_entry_free,
				     NULL);
	mutex_destroy(&policies->lock);
}

int vgpu_policy_validate(const struct vgpu_policy *policy)
{
	if (!policy)
		return -EINVAL;
	if (policy->tgid <= 0)
		return -EINVAL;
	if (policy->gpu_minor < 0)
		return -EINVAL;
	if (policy->flags & ~VGPU_POLICY_VALID_FLAGS)
		return -EINVAL;
	if (policy->compute_weight < 1 || policy->compute_weight > 10000)
		return -EINVAL;
	if ((policy->flags & VGPU_POLICY_F_MEMORY) &&
	    policy->memory_limit_bytes == 0)
		return -EINVAL;

	return 0;
}

int vgpu_policy_set(struct vgpu_policy_table *policies,
		    const struct vgpu_policy *policy)
{
	struct vgpu_policy_entry *entry;
	struct vgpu_policy_key key;
	int ret;

	if (!policies)
		return -EINVAL;

	ret = vgpu_policy_validate(policy);
	if (ret)
		return ret;

	key.tgid = policy->tgid;
	key.gpu_minor = policy->gpu_minor;

	mutex_lock(&policies->lock);
	entry = rhashtable_lookup_fast(&policies->table, &key,
				       vgpu_policy_rhashtable_params);
	if (entry) {
		entry->policy = *policy;
		mutex_unlock(&policies->lock);
		return 0;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		mutex_unlock(&policies->lock);
		return -ENOMEM;
	}

	entry->key = key;
	entry->policy = *policy;
	ret = rhashtable_insert_fast(&policies->table, &entry->node,
				      vgpu_policy_rhashtable_params);
	if (ret)
		kfree(entry);
	mutex_unlock(&policies->lock);

	return ret;
}

int vgpu_policy_get(struct vgpu_policy_table *policies, __s32 tgid,
		    __s32 gpu_minor, struct vgpu_policy *policy)
{
	struct vgpu_policy_entry *entry;
	struct vgpu_policy_key key = {
		.tgid = tgid,
		.gpu_minor = gpu_minor,
	};

	if (!policies || !policy)
		return -EINVAL;
	if (tgid <= 0 || gpu_minor < 0)
		return -EINVAL;

	mutex_lock(&policies->lock);
	entry = rhashtable_lookup_fast(&policies->table, &key,
				       vgpu_policy_rhashtable_params);
	if (!entry) {
		mutex_unlock(&policies->lock);
		return -ENOENT;
	}

	*policy = entry->policy;
	mutex_unlock(&policies->lock);
	return 0;
}

int vgpu_policy_for_each(struct vgpu_policy_table *policies,
			 int (*fn)(const struct vgpu_policy *policy, void *data),
			 void *data)
{
	struct rhashtable_iter iter;
	struct vgpu_policy_entry *entry;
	int ret = 0;

	if (!policies || !fn)
		return -EINVAL;

	mutex_lock(&policies->lock);
	rhashtable_walk_enter(&policies->table, &iter);
	rhashtable_walk_start(&iter);

	while ((entry = rhashtable_walk_next(&iter)) != NULL) {
		if (IS_ERR(entry)) {
			ret = PTR_ERR(entry);
			if (ret == -EAGAIN) {
				ret = 0;
				continue;
			}
			break;
		}

		ret = fn(&entry->policy, data);
		if (ret)
			break;
	}

	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
	mutex_unlock(&policies->lock);

	return ret;
}
