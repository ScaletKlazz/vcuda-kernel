// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/build_bug.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>

#include "vgpu_ioctl.h"
#include "vgpu_ioctl_arg.h"
#include "vgpu_ioctl_trace.h"
#include "vgpu_ctl.h"
#include "vgpu_debugfs.h"
#include "vgpu_device.h"
#include "vgpu_events.h"
#include "vgpu_nv_ioctl.h"
#include "vgpu_nv_probe.h"
#include "vgpu_policy.h"
#include "vgpu_stats.h"
#include "vgpu_task.h"
#include "vgpu_trace.h"

static bool dry_run = true;
static bool allow_enforce;
static struct vgpu_policy_table vgpu_policies;

module_param(dry_run, bool, 0444);
MODULE_PARM_DESC(dry_run, "start in dry-run mode; enabled by default");

module_param(allow_enforce, bool, 0400);
MODULE_PARM_DESC(allow_enforce, "allow write-capable enforcement paths");

static int __init vgpu_init(void)
{
	enum vgpu_runtime_mode initial_mode = dry_run ? VGPU_MODE_DRY_RUN :
		VGPU_MODE_TRACE_ONLY;
	int ret;

	BUILD_BUG_ON(sizeof(struct vgpu_policy) != 24);
	BUILD_BUG_ON(sizeof(struct vgpu_policy_query) != 40);
	BUILD_BUG_ON(sizeof(struct vgpu_stats) != 112);
	BUILD_BUG_ON(sizeof(struct vgpu_driver_fingerprint) != 40);
	vgpu_stats_init(initial_mode);
	vgpu_events_init();
	vgpu_ioctl_arg_init();
	vgpu_ioctl_trace_init();

	ret = vgpu_policy_table_init(&vgpu_policies);
	if (ret)
		return ret;

	ret = vgpu_task_registry_init();
	if (ret) {
		vgpu_policy_table_destroy(&vgpu_policies);
		return ret;
	}

	ret = vgpu_device_registry_init();
	if (ret) {
		vgpu_task_registry_exit();
		vgpu_policy_table_destroy(&vgpu_policies);
		return ret;
	}

	ret = vgpu_ctl_init(&vgpu_policies);
	if (ret) {
		vgpu_device_registry_exit();
		vgpu_task_registry_exit();
		vgpu_policy_table_destroy(&vgpu_policies);
		return ret;
	}

	ret = vgpu_nv_probe_init();
	if (ret) {
		vgpu_ctl_exit();
		vgpu_device_registry_exit();
		vgpu_task_registry_exit();
		vgpu_policy_table_destroy(&vgpu_policies);
		return ret;
	}

	ret = vgpu_nv_ioctl_init();
	if (ret) {
		vgpu_nv_probe_exit();
		vgpu_ctl_exit();
		vgpu_device_registry_exit();
		vgpu_task_registry_exit();
		vgpu_policy_table_destroy(&vgpu_policies);
		return ret;
	}

	ret = vgpu_debugfs_init(&vgpu_policies);
	if (ret) {
		vgpu_nv_ioctl_exit();
		vgpu_nv_probe_exit();
		vgpu_ctl_exit();
		vgpu_device_registry_exit();
		vgpu_task_registry_exit();
		vgpu_policy_table_destroy(&vgpu_policies);
		return ret;
	}

	vgpu_pr_info("module loading dry_run=%d allow_enforce=%d\n",
		      dry_run, allow_enforce);
	return 0;
}

static void __exit vgpu_exit(void)
{
	vgpu_debugfs_exit();
	vgpu_nv_ioctl_exit();
	vgpu_ioctl_arg_exit();
	vgpu_nv_probe_exit();
	vgpu_ctl_exit();
	vgpu_device_registry_exit();
	vgpu_task_registry_exit();
	vgpu_policy_table_destroy(&vgpu_policies);
	vgpu_pr_info("module unloaded\n");
}

module_init(vgpu_init);
module_exit(vgpu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("vgpu-new contributors");
MODULE_DESCRIPTION("vGPU kernel enforcement module");
