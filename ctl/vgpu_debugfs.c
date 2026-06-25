// SPDX-License-Identifier: GPL-2.0
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "vgpu_debugfs.h"
#include "vgpu_events.h"
#include "vgpu_ioctl_arg.h"
#include "vgpu_ioctl_trace.h"
#include "vgpu_nv_ioctl.h"
#include "vgpu_nv_probe.h"
#include "vgpu_stats.h"
#include "vgpu_task.h"

static struct dentry *vgpu_debugfs_root;
static struct vgpu_policy_table *vgpu_debugfs_policies;
static const char *vgpu_debugfs_mode_name(enum vgpu_runtime_mode mode)
{
	switch (mode) {
	case VGPU_MODE_DISABLED:
		return "disabled";
	case VGPU_MODE_TRACE_ONLY:
		return "trace_only";
	case VGPU_MODE_DRY_RUN:
		return "dry_run";
	case VGPU_MODE_ENFORCING:
		return "enforcing";
	default:
		return "unknown";
	}
}

static int vgpu_debugfs_enabled_show(struct seq_file *seq, void *data)
{
	enum vgpu_runtime_mode mode = vgpu_stats_get_mode();

	(void)data;
	seq_printf(seq, "enabled=%u mode=%s mode_id=%u\n",
		   mode != VGPU_MODE_DISABLED,
		   vgpu_debugfs_mode_name(mode), mode);
	return 0;
}

static int vgpu_debugfs_driver_fingerprint_show(struct seq_file *seq,
						void *data)
{
	struct vgpu_driver_fingerprint fp;

	(void)data;
	vgpu_nv_probe_get_fingerprint(&fp);
	seq_printf(seq,
		   "driver_major=%u driver_minor=%u driver_patch=%u okm_detected=%u gsp_enabled=%u capabilities=%u symbol_hash=%llu probe_only=%u memory_dry_run=%u memory_enforce=%u compute_dry_run=%u compute_enforce=%u\n",
		   fp.driver_major, fp.driver_minor, fp.driver_patch,
		   fp.okm_detected, fp.gsp_enabled, fp.capabilities,
		   (unsigned long long)fp.symbol_hash,
		   !!(fp.capabilities & VGPU_CAP_PROBE_ONLY),
		   !!(fp.capabilities & VGPU_CAP_MEMORY_DRY_RUN),
		   !!(fp.capabilities & VGPU_CAP_MEMORY_ENFORCE),
		   !!(fp.capabilities & VGPU_CAP_COMPUTE_DRY_RUN),
		   !!(fp.capabilities & VGPU_CAP_COMPUTE_ENFORCE));
	return 0;
}

static int vgpu_debugfs_hooks_show(struct seq_file *seq, void *data)
{
	struct vgpu_nv_ioctl_status status;

	(void)data;
	vgpu_nv_ioctl_get_status(&status);
	seq_printf(seq,
		   "nvidia_device_hooks=%u open_hooked=%u ioctl_hooked=%u release_hooked=%u memory_hooks=%u compute_hooks=0 hook_errors=%u nvidia_major=%u nvidia_uvm_major=%u memory_alloc_ioctl_cmd=0x%08x memory_ioctl_class_offset=%u memory_alloc_class=0x%08x memory_alloc_type=0x%08x memory_alloc_flags=0x%08x memory_alloc_attr=0x%08x memory_alloc_attr2=0x%08x memory_alloc_tag=0x%08x memory_alloc_min_bytes=%llu memory_alloc_max_bytes=%llu memory_free_ioctl_cmd=0x%08x memory_ioctl_size_offset=%u memory_ioctl_nested_ptr_offset=%u memory_ioctl_nested_size_offset=%u memory_ioctl_size_filter_value=%llu memory_ioctl_size_max_bytes=%llu memory_ioctl_size_alignment=%u ioctl_arg_sample_cmd=0x%08x\n",
		   status.open_hooked || status.ioctl_hooked ||
		   status.release_hooked,
		   status.open_hooked, status.ioctl_hooked,
		   status.release_hooked, status.memory_trace_enabled,
		   status.hook_errors, status.nvidia_major,
		   status.nvidia_uvm_major, status.memory_alloc_ioctl_cmd,
		   status.memory_ioctl_class_offset, status.memory_alloc_class,
		   status.memory_alloc_type, status.memory_alloc_flags,
		   status.memory_alloc_attr, status.memory_alloc_attr2,
		   status.memory_alloc_tag,
		   (unsigned long long)status.memory_alloc_min_bytes,
		   (unsigned long long)status.memory_alloc_max_bytes,
		   status.memory_free_ioctl_cmd, status.memory_ioctl_size_offset,
		   status.memory_ioctl_nested_ptr_offset,
		   status.memory_ioctl_nested_size_offset,
		   (unsigned long long)status.memory_ioctl_size_filter_value,
		   (unsigned long long)status.memory_ioctl_size_max_bytes,
		   status.memory_ioctl_size_alignment,
		   status.ioctl_arg_sample_cmd);
	return 0;
}

static int vgpu_debugfs_policy_print(const struct vgpu_policy *policy,
				     void *data)
{
	struct seq_file *seq = data;

	seq_printf(seq,
		   "tgid=%d gpu_minor=%d memory_limit_bytes=%llu compute_weight=%u flags=%u memory=%u compute=%u dry_run=%u\n",
		   policy->tgid, policy->gpu_minor,
		   (unsigned long long)policy->memory_limit_bytes,
		   policy->compute_weight, policy->flags,
		   !!(policy->flags & VGPU_POLICY_F_MEMORY),
		   !!(policy->flags & VGPU_POLICY_F_COMPUTE),
		   !!(policy->flags & VGPU_POLICY_F_DRY_RUN));
	return 0;
}

static int vgpu_debugfs_policies_show(struct seq_file *seq, void *data)
{
	int ret;

	(void)data;
	if (!vgpu_debugfs_policies)
		return -ENODEV;

	ret = vgpu_policy_for_each(vgpu_debugfs_policies,
				   vgpu_debugfs_policy_print, seq);
	return ret;
}

static int vgpu_debugfs_task_print(const struct vgpu_task_snapshot *task,
				   void *data)
{
	struct seq_file *seq = data;

	seq_printf(seq,
		   "pid=%d tgid=%d gpu_minor=%d nvidia_major=%u fd_refs=%u memory_used_bytes=%llu last_timeslice=%llu last_seen_jiffies=%llu\n",
		   task->pid, task->tgid, task->gpu_minor, task->nvidia_major,
		   task->fd_refs,
		   (unsigned long long)task->memory_used_bytes,
		   (unsigned long long)task->last_timeslice,
		   (unsigned long long)task->last_seen_jiffies);
	return 0;
}

static int vgpu_debugfs_tasks_show(struct seq_file *seq, void *data)
{
	int ret;

	(void)data;
	ret = vgpu_task_for_each(vgpu_debugfs_task_print, seq);
	return ret;
}

static int vgpu_debugfs_events_show(struct seq_file *seq, void *data)
{
	struct vgpu_event_record *records;
	size_t count;
	size_t i;

	(void)data;
	records = kcalloc(VGPU_EVENT_RING_SIZE, sizeof(*records), GFP_KERNEL);
	if (!records)
		return -ENOMEM;

	count = vgpu_events_snapshot(records, VGPU_EVENT_RING_SIZE);
	for (i = 0; i < count; i++) {
		seq_printf(seq,
			   "seq=%llu ts_ns=%llu type=%u pid=%d tgid=%d gpu_minor=%d old_value=%llu new_value=%llu error=%d flags=%u\n",
			   (unsigned long long)records[i].seq,
			   (unsigned long long)records[i].ts_ns,
			   records[i].type, records[i].pid, records[i].tgid,
			   records[i].gpu_minor,
			   (unsigned long long)records[i].old_value,
			   (unsigned long long)records[i].new_value,
			   records[i].error, records[i].flags);
	}

	kfree(records);
	return 0;
}

static int vgpu_debugfs_ioctls_show(struct seq_file *seq, void *data)
{
	struct vgpu_ioctl_trace_snapshot *snapshots;
	size_t count;
	size_t i;

	(void)data;
	snapshots = kcalloc(VGPU_IOCTL_TRACE_MAX, sizeof(*snapshots),
			    GFP_KERNEL);
	if (!snapshots)
		return -ENOMEM;

	count = vgpu_ioctl_trace_snapshot(snapshots, VGPU_IOCTL_TRACE_MAX);
	for (i = 0; i < count; i++) {
		seq_printf(seq,
			   "cmd=0x%08x count=%llu last_arg=%llu last_pid=%d last_tgid=%d last_gpu_minor=%d last_nvidia_major=%u\n",
			   snapshots[i].cmd,
			   (unsigned long long)snapshots[i].count,
			   (unsigned long long)snapshots[i].last_arg,
			   snapshots[i].last_pid, snapshots[i].last_tgid,
			   snapshots[i].last_gpu_minor,
			   snapshots[i].last_nvidia_major);
	}

	kfree(snapshots);
	return 0;
}

static int vgpu_debugfs_ioctl_args_show(struct seq_file *seq, void *data)
{
	struct vgpu_ioctl_arg_snapshot *snapshots;
	size_t count;
	size_t i;

	(void)data;
	snapshots = kcalloc(VGPU_IOCTL_ARG_MAX, sizeof(*snapshots),
			    GFP_KERNEL);
	if (!snapshots)
		return -ENOMEM;

	count = vgpu_ioctl_arg_snapshot(snapshots, VGPU_IOCTL_ARG_MAX);
	for (i = 0; i < count; i++) {
		if (vgpu_ioctl_arg_filter_value != 0 &&
		    snapshots[i].filter_u32_hits == 0 &&
		    snapshots[i].filter_u64_hits == 0)
			continue;
		seq_printf(seq,
			   "cmd=0x%08x offset=%u count=%llu filter_u32_hits=%llu filter_u64_hits=%llu last_u32=%u min_u32=%u max_u32=%u last_u64=%llu min_u64=%llu max_u64=%llu\n",
			   snapshots[i].cmd, snapshots[i].offset,
			   (unsigned long long)snapshots[i].count,
			   (unsigned long long)snapshots[i].filter_u32_hits,
			   (unsigned long long)snapshots[i].filter_u64_hits,
			   snapshots[i].last_u32,
			   snapshots[i].min_u32,
			   snapshots[i].max_u32,
			   (unsigned long long)snapshots[i].last_u64,
			   (unsigned long long)snapshots[i].min_u64,
			   (unsigned long long)snapshots[i].max_u64);
	}

	kfree(snapshots);
	return 0;
}

static int vgpu_debugfs_ioctl_arg_values_show(struct seq_file *seq, void *data)
{
	struct vgpu_ioctl_arg_value_snapshot *snapshots;
	size_t count;
	size_t i;

	(void)data;
	snapshots = kcalloc(VGPU_IOCTL_ARG_VALUE_MAX, sizeof(*snapshots),
			    GFP_KERNEL);
	if (!snapshots)
		return -ENOMEM;

	count = vgpu_ioctl_arg_value_snapshot(snapshots,
					       VGPU_IOCTL_ARG_VALUE_MAX);
	for (i = 0; i < count; i++) {
		if (vgpu_ioctl_arg_value_min_count != 0 &&
		    snapshots[i].count < vgpu_ioctl_arg_value_min_count)
			continue;
		seq_printf(seq,
			   "cmd=0x%08x offset=%u value=0x%08x value_dec=%u count=%llu\n",
			   snapshots[i].cmd, snapshots[i].offset,
			   snapshots[i].value, snapshots[i].value,
			   (unsigned long long)snapshots[i].count);
	}

	kfree(snapshots);
	return 0;
}

static int vgpu_debugfs_rm_controls_show(struct seq_file *seq, void *data)
{
	return vgpu_debugfs_ioctl_arg_values_show(seq, data);
}

static int vgpu_debugfs_alloc_pairs_show(struct seq_file *seq, void *data)
{
	struct vgpu_ioctl_arg_pair_snapshot *snapshots;
	size_t count;
	size_t i;

	(void)data;
	snapshots = kcalloc(VGPU_IOCTL_ARG_PAIR_MAX, sizeof(*snapshots),
			    GFP_KERNEL);
	if (!snapshots)
		return -ENOMEM;

	count = vgpu_ioctl_arg_pair_snapshot(snapshots,
					      VGPU_IOCTL_ARG_PAIR_MAX);
	for (i = 0; i < count; i++) {
		seq_printf(seq,
			   "cmd=0x%08x hclass=0x%08x size=%llu count=%llu\n",
			   snapshots[i].cmd, snapshots[i].class_value,
			   (unsigned long long)snapshots[i].size_value,
			   (unsigned long long)snapshots[i].count);
	}

	kfree(snapshots);
	return 0;
}

static int vgpu_debugfs_alloc_details_show(struct seq_file *seq, void *data)
{
	struct vgpu_ioctl_arg_detail_snapshot *snapshots;
	size_t count;
	size_t i;

	(void)data;
	snapshots = kcalloc(VGPU_IOCTL_ARG_DETAIL_MAX, sizeof(*snapshots),
			    GFP_KERNEL);
	if (!snapshots)
		return -ENOMEM;

	count = vgpu_ioctl_arg_detail_snapshot(snapshots,
						VGPU_IOCTL_ARG_DETAIL_MAX);
	for (i = 0; i < count; i++) {
		seq_printf(seq,
			   "cmd=0x%08x parent=0x%08x object=0x%08x hclass=0x%08x owner=0x%08x type=0x%08x flags=0x%08x attr=0x%08x attr2=0x%08x tag=0x%08x size=%llu offset=%llu limit=%llu address=%llu count=%llu\n",
			   snapshots[i].cmd, snapshots[i].parent,
			   snapshots[i].object, snapshots[i].class_value,
			   snapshots[i].owner, snapshots[i].type,
			   snapshots[i].flags, snapshots[i].attr, snapshots[i].attr2,
			   snapshots[i].tag,
			   (unsigned long long)snapshots[i].size_value,
			   (unsigned long long)snapshots[i].offset,
			   (unsigned long long)snapshots[i].limit,
			   (unsigned long long)snapshots[i].address,
			   (unsigned long long)snapshots[i].count);
	}

	kfree(snapshots);
	return 0;
}

static int vgpu_debugfs_stats_show(struct seq_file *seq, void *data)
{
	struct vgpu_stats stats;

	(void)data;
	vgpu_stats_snapshot(&stats);
	seq_printf(seq,
		   "runtime_mode=%u policies_set=%llu ioctl_seen=%llu memory_alloc_seen=%llu memory_free_seen=%llu memory_would_deny=%llu memory_denied=%llu timeslice_seen=%llu timeslice_would_rewrite=%llu timeslice_rewritten=%llu hook_errors=%llu emergency_disabled=%llu events_dropped=%llu unmatched_free=%llu\n",
		   stats.runtime_mode,
		   (unsigned long long)stats.policies_set,
		   (unsigned long long)stats.ioctl_seen,
		   (unsigned long long)stats.memory_alloc_seen,
		   (unsigned long long)stats.memory_free_seen,
		   (unsigned long long)stats.memory_would_deny,
		   (unsigned long long)stats.memory_denied,
		   (unsigned long long)stats.timeslice_seen,
		   (unsigned long long)stats.timeslice_would_rewrite,
		   (unsigned long long)stats.timeslice_rewritten,
		   (unsigned long long)stats.hook_errors,
		   (unsigned long long)stats.emergency_disabled,
		   (unsigned long long)stats.events_dropped,
		   (unsigned long long)stats.unmatched_free);
	return 0;
}

#define VGPU_DEBUGFS_OPEN(name) \
static int name##_open(struct inode *inode, struct file *file) \
{ \
	return single_open(file, name##_show, inode->i_private); \
}

#define VGPU_DEBUGFS_FOPS(name) \
VGPU_DEBUGFS_OPEN(name) \
static const struct file_operations name##_fops = { \
	.owner = THIS_MODULE, \
	.open = name##_open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = single_release, \
}

VGPU_DEBUGFS_FOPS(vgpu_debugfs_enabled);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_driver_fingerprint);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_hooks);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_policies);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_tasks);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_events);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_ioctls);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_ioctl_args);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_ioctl_arg_values);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_rm_controls);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_alloc_pairs);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_alloc_details);
VGPU_DEBUGFS_FOPS(vgpu_debugfs_stats);

static int vgpu_debugfs_create_file(const char *name,
				    const struct file_operations *fops)
{
	struct dentry *entry;

	entry = debugfs_create_file(name, 0444, vgpu_debugfs_root, NULL, fops);
	if (IS_ERR(entry))
		return PTR_ERR(entry);
	if (!entry)
		return -ENOMEM;

	return 0;
}

int vgpu_debugfs_init(struct vgpu_policy_table *policies)
{
	int ret;

	if (!policies)
		return -EINVAL;

	vgpu_debugfs_policies = policies;
	vgpu_debugfs_root = debugfs_create_dir("vgpu", NULL);
	if (IS_ERR(vgpu_debugfs_root)) {
		vgpu_debugfs_policies = NULL;
		return PTR_ERR(vgpu_debugfs_root);
	}
	if (!vgpu_debugfs_root) {
		vgpu_debugfs_policies = NULL;
		return -ENOMEM;
	}

	ret = vgpu_debugfs_create_file("enabled", &vgpu_debugfs_enabled_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("driver_fingerprint",
				       &vgpu_debugfs_driver_fingerprint_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("hooks", &vgpu_debugfs_hooks_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("policies", &vgpu_debugfs_policies_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("tasks", &vgpu_debugfs_tasks_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("events", &vgpu_debugfs_events_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("ioctls", &vgpu_debugfs_ioctls_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("ioctl_args",
				       &vgpu_debugfs_ioctl_args_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("ioctl_arg_values",
				       &vgpu_debugfs_ioctl_arg_values_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("rm_controls",
				       &vgpu_debugfs_rm_controls_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("alloc_pairs",
				       &vgpu_debugfs_alloc_pairs_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("alloc_details",
				       &vgpu_debugfs_alloc_details_fops);
	if (ret)
		goto err_remove;
	ret = vgpu_debugfs_create_file("stats", &vgpu_debugfs_stats_fops);
	if (ret)
		goto err_remove;

	return 0;

err_remove:
	debugfs_remove_recursive(vgpu_debugfs_root);
	vgpu_debugfs_root = NULL;
	vgpu_debugfs_policies = NULL;
	return ret;
}

void vgpu_debugfs_exit(void)
{
	debugfs_remove_recursive(vgpu_debugfs_root);
	vgpu_debugfs_root = NULL;
	vgpu_debugfs_policies = NULL;
}
