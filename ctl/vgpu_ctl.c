// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "vgpu_ctl.h"
#include "vgpu_events.h"
#include "vgpu_ioctl.h"
#include "vgpu_stats.h"

static struct vgpu_policy_table *vgpu_ctl_policies;
static struct vgpu_cgroup_policy_table *vgpu_ctl_cgroup_policies;
static DEFINE_MUTEX(vgpu_ctl_lock);

static int vgpu_ctl_set_policy(unsigned long arg)
{
	struct vgpu_policy policy;
	int ret;

	if (copy_from_user(&policy, (void __user *)arg, sizeof(policy)))
		return -EFAULT;

	ret = vgpu_policy_set(vgpu_ctl_policies, &policy);
	if (ret)
		return ret;

	vgpu_stats_inc(VGPU_STAT_POLICIES_SET);
	vgpu_events_push(VGPU_EVENT_POLICY_SET, 0, policy.tgid,
			 policy.gpu_minor, policy.memory_limit_bytes,
			 policy.compute_weight, 0, policy.flags);
	return 0;
}

static int vgpu_ctl_get_policy(unsigned long arg)
{
	struct vgpu_policy_query query;
	int ret;

	if (copy_from_user(&query, (void __user *)arg, sizeof(query)))
		return -EFAULT;

	ret = vgpu_policy_get(vgpu_ctl_policies, query.tgid, query.gpu_minor,
			      &query.policy);
	if (ret == -ENOENT) {
		query.found = 0;
		memset(&query.policy, 0, sizeof(query.policy));
		ret = 0;
	} else if (!ret) {
		query.found = 1;
	}

	if (ret)
		return ret;

	if (copy_to_user((void __user *)arg, &query, sizeof(query)))
		return -EFAULT;

	return 0;
}

static int vgpu_ctl_set_cgroup_policy(unsigned long arg)
{
	struct vgpu_cgroup_policy policy;
	int ret;

	if (copy_from_user(&policy, (void __user *)arg, sizeof(policy)))
		return -EFAULT;

	ret = vgpu_cgroup_policy_set(vgpu_ctl_cgroup_policies, &policy);
	if (ret)
		return ret;

	vgpu_stats_inc(VGPU_STAT_POLICIES_SET);
	vgpu_events_push(VGPU_EVENT_POLICY_SET, 0, 0, policy.gpu_minor,
			 policy.memory_limit_bytes, policy.compute_weight, 0,
			 policy.flags);
	return 0;
}

static int vgpu_ctl_get_cgroup_policy(unsigned long arg)
{
	struct vgpu_cgroup_policy_query query;
	int ret;

	if (copy_from_user(&query, (void __user *)arg, sizeof(query)))
		return -EFAULT;

	ret = vgpu_cgroup_policy_get(vgpu_ctl_cgroup_policies, query.cgroup_id,
				      query.gpu_minor, &query.policy);
	if (ret == -ENOENT) {
		query.found = 0;
		memset(&query.policy, 0, sizeof(query.policy));
		ret = 0;
	} else if (!ret) {
		query.found = 1;
	}

	if (ret)
		return ret;

	if (copy_to_user((void __user *)arg, &query, sizeof(query)))
		return -EFAULT;

	return 0;
}

static int vgpu_ctl_get_stats(unsigned long arg)
{
	struct vgpu_stats stats;

	vgpu_stats_snapshot(&stats);
	if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
		return -EFAULT;

	return 0;
}

static int vgpu_ctl_set_dry_run(unsigned long arg)
{
	int enabled;
	int ret = 0;

	if (copy_from_user(&enabled, (void __user *)arg, sizeof(enabled)))
		return -EFAULT;

	mutex_lock(&vgpu_ctl_lock);
	if (vgpu_stats_get_mode() == VGPU_MODE_DISABLED) {
		ret = -EACCES;
	} else if (enabled) {
		vgpu_stats_set_mode(VGPU_MODE_DRY_RUN);
	} else {
		vgpu_stats_set_mode(VGPU_MODE_TRACE_ONLY);
	}
	mutex_unlock(&vgpu_ctl_lock);

	return ret;
}

static int vgpu_ctl_disable(void)
{
	enum vgpu_runtime_mode previous;

	mutex_lock(&vgpu_ctl_lock);
	previous = vgpu_stats_get_mode();
	if (previous != VGPU_MODE_DISABLED) {
		vgpu_stats_set_mode(VGPU_MODE_DISABLED);
		vgpu_stats_inc(VGPU_STAT_EMERGENCY_DISABLED);
		vgpu_events_push(VGPU_EVENT_EMERGENCY_DISABLED, 0, 0, -1,
				 previous, VGPU_MODE_DISABLED, 0, 0);
	}
	mutex_unlock(&vgpu_ctl_lock);

	return 0;
}

static long vgpu_ctl_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	(void)file;

	if (!vgpu_ctl_policies || !vgpu_ctl_cgroup_policies)
		return -ENODEV;

	switch (cmd) {
	case VGPU_IOCTL_SET_POLICY:
		return vgpu_ctl_set_policy(arg);
	case VGPU_IOCTL_GET_POLICY:
		return vgpu_ctl_get_policy(arg);
	case VGPU_IOCTL_SET_CGROUP_POLICY:
		return vgpu_ctl_set_cgroup_policy(arg);
	case VGPU_IOCTL_GET_CGROUP_POLICY:
		return vgpu_ctl_get_cgroup_policy(arg);
	case VGPU_IOCTL_GET_STATS:
		return vgpu_ctl_get_stats(arg);
	case VGPU_IOCTL_SET_DRY_RUN:
		return vgpu_ctl_set_dry_run(arg);
	case VGPU_IOCTL_DISABLE:
		return vgpu_ctl_disable();
	default:
		return -ENOTTY;
	}
}

static const struct file_operations vgpu_ctl_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vgpu_ctl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vgpu_ctl_ioctl,
#endif
};

static struct miscdevice vgpu_ctl_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "vgpuctl",
	.fops = &vgpu_ctl_fops,
};

int vgpu_ctl_init(struct vgpu_policy_table *policies,
		  struct vgpu_cgroup_policy_table *cgroup_policies)
{
	int ret;

	if (!policies || !cgroup_policies)
		return -EINVAL;

	vgpu_ctl_policies = policies;
	vgpu_ctl_cgroup_policies = cgroup_policies;
	ret = misc_register(&vgpu_ctl_miscdev);
	if (ret) {
		vgpu_ctl_policies = NULL;
		vgpu_ctl_cgroup_policies = NULL;
	}

	return ret;
}

void vgpu_ctl_exit(void)
{
	misc_deregister(&vgpu_ctl_miscdev);
	vgpu_ctl_policies = NULL;
	vgpu_ctl_cgroup_policies = NULL;
}
