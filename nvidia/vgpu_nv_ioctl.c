// SPDX-License-Identifier: GPL-2.0-only
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/kdev_t.h>
#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "vgpu_device.h"
#include "vgpu_events.h"
#include "vgpu_ioctl_arg.h"
#include "vgpu_ioctl_trace.h"
#include "vgpu_nv_ioctl.h"
#include "vgpu_stats.h"
#include "vgpu_task.h"
#include "vgpu_trace.h"

static uint nvidia_major = 195;
static uint nvidia_uvm_major = 511;
static bool trace_nvidia_devices = true;
static uint memory_alloc_ioctl_cmd;
static uint memory_free_ioctl_cmd;
static bool memory_ioctl_arg_is_size;
static uint memory_ioctl_class_offset = UINT_MAX;
static uint memory_alloc_class;
static uint memory_alloc_type = UINT_MAX;
static uint memory_alloc_flags = UINT_MAX;
static uint memory_alloc_attr = UINT_MAX;
static uint memory_alloc_attr2 = UINT_MAX;
static uint memory_alloc_tag = UINT_MAX;
static unsigned long memory_alloc_min_bytes;
static unsigned long memory_alloc_max_bytes;
static uint memory_ioctl_size_offset = UINT_MAX;
static uint memory_ioctl_nested_ptr_offset = UINT_MAX;
static uint memory_ioctl_nested_size_offset = UINT_MAX;
static unsigned long memory_ioctl_size_filter_value;
static unsigned long memory_ioctl_size_max_bytes;
static uint memory_ioctl_size_alignment = 4096;
static unsigned long memory_trace_limit_bytes;
static uint ioctl_arg_sample_cmd;
static char ioctl_arg_sample_cmds[256];
static uint ioctl_arg_nested_ptr_offset = UINT_MAX;
static uint ioctl_arg_nested_sample_bytes = VGPU_IOCTL_ARG_BYTES;

module_param(nvidia_major, uint, 0444);
MODULE_PARM_DESC(nvidia_major, "primary NVIDIA character device major");
module_param(nvidia_uvm_major, uint, 0444);
MODULE_PARM_DESC(nvidia_uvm_major, "NVIDIA UVM character device major");
module_param(trace_nvidia_devices, bool, 0644);
MODULE_PARM_DESC(trace_nvidia_devices, "trace NVIDIA character device calls");
module_param(memory_alloc_ioctl_cmd, uint, 0644);
MODULE_PARM_DESC(memory_alloc_ioctl_cmd, "ioctl command treated as GPU memory allocation; 0 disables");
module_param(memory_free_ioctl_cmd, uint, 0644);
MODULE_PARM_DESC(memory_free_ioctl_cmd, "ioctl command treated as GPU memory free; 0 disables");
module_param(memory_ioctl_arg_is_size, bool, 0644);
MODULE_PARM_DESC(memory_ioctl_arg_is_size, "treat ioctl arg value as memory size; disabled by default");
module_param(memory_ioctl_class_offset, uint, 0644);
MODULE_PARM_DESC(memory_ioctl_class_offset, "read RM alloc hClass as u32 from ioctl arg + offset; UINT_MAX disables");
module_param(memory_alloc_class, uint, 0644);
MODULE_PARM_DESC(memory_alloc_class, "only account RM allocs with this hClass; 0 disables class filtering");
module_param(memory_alloc_type, uint, 0644);
MODULE_PARM_DESC(memory_alloc_type, "only account RM allocs with this NV_MEMORY_ALLOCATION_PARAMS.type; UINT_MAX disables");
module_param(memory_alloc_flags, uint, 0644);
MODULE_PARM_DESC(memory_alloc_flags, "only account RM allocs with this NV_MEMORY_ALLOCATION_PARAMS.flags; UINT_MAX disables");
module_param(memory_alloc_attr, uint, 0644);
MODULE_PARM_DESC(memory_alloc_attr, "only account RM allocs with this NV_MEMORY_ALLOCATION_PARAMS.attr; UINT_MAX disables");
module_param(memory_alloc_attr2, uint, 0644);
MODULE_PARM_DESC(memory_alloc_attr2, "only account RM allocs with this NV_MEMORY_ALLOCATION_PARAMS.attr2; UINT_MAX disables");
module_param(memory_alloc_tag, uint, 0644);
MODULE_PARM_DESC(memory_alloc_tag, "only account RM allocs with this NV_MEMORY_ALLOCATION_PARAMS.tag; UINT_MAX disables");
module_param(memory_alloc_min_bytes, ulong, 0644);
MODULE_PARM_DESC(memory_alloc_min_bytes, "only account memory allocs at least this size; 0 disables");
module_param(memory_alloc_max_bytes, ulong, 0644);
MODULE_PARM_DESC(memory_alloc_max_bytes, "only account memory allocs at most this size; 0 disables");
module_param(memory_ioctl_size_offset, uint, 0644);
MODULE_PARM_DESC(memory_ioctl_size_offset, "read memory size as u64 from ioctl arg + offset; UINT_MAX disables");
module_param(memory_ioctl_nested_ptr_offset, uint, 0644);
MODULE_PARM_DESC(memory_ioctl_nested_ptr_offset, "read nested parameter pointer from ioctl arg + offset; UINT_MAX disables");
module_param(memory_ioctl_nested_size_offset, uint, 0644);
MODULE_PARM_DESC(memory_ioctl_nested_size_offset, "read memory size as u64 from nested parameter pointer + offset; UINT_MAX disables");
module_param(memory_ioctl_size_filter_value, ulong, 0644);
MODULE_PARM_DESC(memory_ioctl_size_filter_value, "only account sampled memory sizes equal to this value; 0 disables");
module_param(memory_ioctl_size_max_bytes, ulong, 0644);
MODULE_PARM_DESC(memory_ioctl_size_max_bytes, "ignore sampled memory sizes above this value; 0 disables");
module_param(memory_ioctl_size_alignment, uint, 0644);
MODULE_PARM_DESC(memory_ioctl_size_alignment, "required sampled memory size alignment; 0 disables");
module_param(memory_trace_limit_bytes, ulong, 0644);
MODULE_PARM_DESC(memory_trace_limit_bytes, "optional dry-run memory limit for configurable memory ioctl tracing");
module_param(ioctl_arg_sample_cmd, uint, 0644);
MODULE_PARM_DESC(ioctl_arg_sample_cmd, "ioctl command whose user arg struct is sampled with copy_from_user_nofault; 0 disables");
module_param_string(ioctl_arg_sample_cmds, ioctl_arg_sample_cmds,
		    sizeof(ioctl_arg_sample_cmds), 0644);
MODULE_PARM_DESC(ioctl_arg_sample_cmds, "comma/space separated ioctl commands sampled with copy_from_user_nofault");
module_param(ioctl_arg_nested_ptr_offset, uint, 0644);
MODULE_PARM_DESC(ioctl_arg_nested_ptr_offset, "sample pointed argument buffer from this ioctl arg pointer offset; UINT_MAX disables");
module_param(ioctl_arg_nested_sample_bytes, uint, 0644);
MODULE_PARM_DESC(ioctl_arg_nested_sample_bytes, "bytes to sample from nested ioctl pointer buffer");

static struct vgpu_nv_ioctl_status vgpu_nv_ioctl_status;

static bool vgpu_nv_is_nvidia_major(unsigned int major)
{
	return major == nvidia_major ||
	       (nvidia_uvm_major != 0 && major == nvidia_uvm_major);
}

static bool vgpu_nv_read_memory_size(unsigned long arg, __u64 *bytes)
{
	unsigned long size_addr;
	unsigned long nested_ptr;
	unsigned long ptr_addr;

	if (!bytes || arg == 0)
		return false;

	if (memory_ioctl_arg_is_size) {
		*bytes = (__u64)arg;
		return *bytes != 0;
	}

	if (memory_ioctl_size_offset != UINT_MAX) {
		if (arg > ULONG_MAX - memory_ioctl_size_offset)
			return false;
		size_addr = arg + memory_ioctl_size_offset;
		goto read_size;
	}

	if (memory_ioctl_nested_ptr_offset == UINT_MAX ||
	    memory_ioctl_nested_size_offset == UINT_MAX)
		return false;
	if (arg > ULONG_MAX - memory_ioctl_nested_ptr_offset)
		return false;

	ptr_addr = arg + memory_ioctl_nested_ptr_offset;
	if (copy_from_user_nofault(&nested_ptr, (const void __user *)ptr_addr,
				   sizeof(nested_ptr)))
		return false;
	if (!nested_ptr || nested_ptr > ULONG_MAX - memory_ioctl_nested_size_offset)
		return false;
	size_addr = nested_ptr + memory_ioctl_nested_size_offset;

read_size:
	if (copy_from_user_nofault(bytes, (const void __user *)size_addr,
				   sizeof(*bytes)))
		return false;

	if (memory_ioctl_size_alignment != 0 &&
	    (*bytes % memory_ioctl_size_alignment) != 0)
		return false;
	if (memory_ioctl_size_filter_value != 0 &&
	    *bytes != memory_ioctl_size_filter_value)
		return false;
	if (memory_ioctl_size_max_bytes != 0 &&
	    *bytes > memory_ioctl_size_max_bytes)
		return false;

	return *bytes != 0;
}

static bool vgpu_nv_read_nested_ptr(unsigned long arg, unsigned long *nested_ptr)
{
	unsigned long ptr_addr;

	if (!nested_ptr || arg == 0 || memory_ioctl_nested_ptr_offset == UINT_MAX)
		return false;
	if (arg > ULONG_MAX - memory_ioctl_nested_ptr_offset)
		return false;

	ptr_addr = arg + memory_ioctl_nested_ptr_offset;
	if (copy_from_user_nofault(nested_ptr, (const void __user *)ptr_addr,
				   sizeof(*nested_ptr)))
		return false;

	return *nested_ptr != 0;
}

static bool vgpu_nv_read_memory_class(unsigned long arg, __u32 *hclass)
{
	unsigned long class_addr;

	if (!hclass || arg == 0 || memory_ioctl_class_offset == UINT_MAX)
		return false;
	if (arg > ULONG_MAX - memory_ioctl_class_offset)
		return false;

	class_addr = arg + memory_ioctl_class_offset;
	if (copy_from_user_nofault(hclass, (const void __user *)class_addr,
				   sizeof(*hclass)))
		return false;

	return true;
}

static bool vgpu_nv_memory_class_match(__u32 hclass)
{
	return memory_alloc_class == 0 || hclass == memory_alloc_class;
}

static bool vgpu_nv_read_alloc_detail(unsigned long arg, __u32 hclass,
				      __u64 bytes,
				      struct vgpu_ioctl_arg_detail_snapshot *detail)
{
	unsigned long nested_ptr;

	if (!detail)
		return false;
	if (!vgpu_nv_read_nested_ptr(arg, &nested_ptr))
		return false;
	if (nested_ptr > ULONG_MAX - 104)
		return false;

	memset(detail, 0, sizeof(*detail));
	detail->class_value = hclass;
	detail->size_value = bytes;
	if (copy_from_user_nofault(&detail->parent,
				   (const void __user *)(arg + 4),
				   sizeof(detail->parent)))
		return false;
	if (copy_from_user_nofault(&detail->object,
				   (const void __user *)(arg + 8),
				   sizeof(detail->object)))
		return false;
	if (copy_from_user_nofault(&detail->owner,
				   (const void __user *)(nested_ptr + 0),
				   sizeof(detail->owner)))
		return false;
	if (copy_from_user_nofault(&detail->type,
				   (const void __user *)(nested_ptr + 4),
				   sizeof(detail->type)))
		return false;
	if (copy_from_user_nofault(&detail->flags,
				   (const void __user *)(nested_ptr + 8),
				   sizeof(detail->flags)))
		return false;
	if (copy_from_user_nofault(&detail->attr,
				   (const void __user *)(nested_ptr + 24),
				   sizeof(detail->attr)))
		return false;
	if (copy_from_user_nofault(&detail->attr2,
				   (const void __user *)(nested_ptr + 28),
				   sizeof(detail->attr2)))
		return false;
	if (copy_from_user_nofault(&detail->offset,
				   (const void __user *)(nested_ptr + 72),
				   sizeof(detail->offset)))
		return false;
	if (copy_from_user_nofault(&detail->limit,
				   (const void __user *)(nested_ptr + 80),
				   sizeof(detail->limit)))
		return false;
	if (copy_from_user_nofault(&detail->address,
				   (const void __user *)(nested_ptr + 88),
				   sizeof(detail->address)))
		return false;
	if (copy_from_user_nofault(&detail->tag,
				   (const void __user *)(nested_ptr + 96),
				   sizeof(detail->tag)))
		return false;

	return true;
}

static bool vgpu_nv_memory_detail_match(const struct vgpu_ioctl_arg_detail_snapshot *detail)
{
	if (!detail)
		return memory_alloc_type == UINT_MAX &&
		       memory_alloc_flags == UINT_MAX &&
		       memory_alloc_attr == UINT_MAX &&
		       memory_alloc_attr2 == UINT_MAX &&
		       memory_alloc_tag == UINT_MAX;
	if (memory_alloc_type != UINT_MAX && detail->type != memory_alloc_type)
		return false;
	if (memory_alloc_flags != UINT_MAX && detail->flags != memory_alloc_flags)
		return false;
	if (memory_alloc_attr != UINT_MAX && detail->attr != memory_alloc_attr)
		return false;
	if (memory_alloc_attr2 != UINT_MAX && detail->attr2 != memory_alloc_attr2)
		return false;
	if (memory_alloc_tag != UINT_MAX && detail->tag != memory_alloc_tag)
		return false;

	return true;
}

static bool vgpu_nv_memory_size_match(__u64 bytes)
{
	if (memory_alloc_min_bytes != 0 && bytes < memory_alloc_min_bytes)
		return false;
	if (memory_alloc_max_bytes != 0 && bytes > memory_alloc_max_bytes)
		return false;

	return true;
}

static size_t vgpu_nv_parse_sample_cmds(__u32 *cmds, size_t max_cmds)
{
	char buffer[sizeof(ioctl_arg_sample_cmds)];
	char *cursor;
	char *token;
	size_t count = 0;
	unsigned int value;

	if (!cmds || max_cmds == 0)
		return 0;

	if (ioctl_arg_sample_cmd != 0)
		cmds[count++] = ioctl_arg_sample_cmd;

	strscpy(buffer, ioctl_arg_sample_cmds, sizeof(buffer));
	cursor = buffer;
	while (count < max_cmds &&
	       (token = strsep(&cursor, ", \t\n")) != NULL) {
		if (*token == '\0')
			continue;
		if (kstrtouint(token, 0, &value))
			continue;
		cmds[count++] = value;
	}

	return count;
}

static bool vgpu_nv_file_match(struct file *file, int *gpu_minor,
			       unsigned int *major_out)
{
	struct inode *inode;
	unsigned int major;

	if (!file)
		return false;

	inode = file_inode(file);
	if (!inode || !S_ISCHR(inode->i_mode))
		return false;

	major = MAJOR(inode->i_rdev);
	if (!vgpu_nv_is_nvidia_major(major))
		return false;

	if (gpu_minor)
		*gpu_minor = MINOR(inode->i_rdev);
	if (major_out)
		*major_out = major;
	return true;
}

static bool vgpu_nv_inode_match(struct inode *inode, int *gpu_minor,
				unsigned int *major_out)
{
	unsigned int major;

	if (!inode || !S_ISCHR(inode->i_mode))
		return false;

	major = MAJOR(inode->i_rdev);
	if (!vgpu_nv_is_nvidia_major(major))
		return false;

	if (gpu_minor)
		*gpu_minor = MINOR(inode->i_rdev);
	if (major_out)
		*major_out = major;
	return true;
}

#ifdef CONFIG_X86_64
static unsigned long vgpu_arg0(struct pt_regs *regs)
{
	return regs->di;
}

static unsigned long vgpu_arg1(struct pt_regs *regs)
{
	return regs->si;
}

static unsigned long vgpu_arg2(struct pt_regs *regs)
{
	return regs->dx;
}

static unsigned long vgpu_arg3(struct pt_regs *regs)
{
	return regs->cx;
}
#else
static unsigned long vgpu_arg0(struct pt_regs *regs)
{
	(void)regs;
	return 0;
}

static unsigned long vgpu_arg1(struct pt_regs *regs)
{
	(void)regs;
	return 0;
}

static unsigned long vgpu_arg2(struct pt_regs *regs)
{
	(void)regs;
	return 0;
}

static unsigned long vgpu_arg3(struct pt_regs *regs)
{
	(void)regs;
	return 0;
}
#endif

static int vgpu_nv_chrdev_open_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct inode *inode = (struct inode *)vgpu_arg0(regs);
	struct file *file = (struct file *)vgpu_arg1(regs);
	unsigned int major = 0;
	int gpu_minor = -1;
	int ret;

	(void)p;
	if (!trace_nvidia_devices)
		return 0;
	if (!vgpu_nv_inode_match(inode, &gpu_minor, &major))
		return 0;

	ret = file ? vgpu_device_open_file(file, current->pid, current->tgid,
					   gpu_minor, major) : 0;
	if (ret > 0)
		vgpu_task_open(current->pid, current->tgid, gpu_minor, major);

	vgpu_events_push(VGPU_EVENT_NVIDIA_OPEN, current->pid, current->tgid,
			 gpu_minor, major, 0, 0, major);
	return 0;
}

static int vgpu_nv_ioctl_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct file *file = (struct file *)vgpu_arg0(regs);
	unsigned int cmd = (unsigned int)vgpu_arg2(regs);
	unsigned long arg = vgpu_arg3(regs);
	struct vgpu_device_file device_file;
	__u32 sample_cmds[16];
	size_t sample_cmd_count;
	bool known_file;
	bool would_deny = false;
	__s32 task_tgid;
	__s32 task_gpu_minor;
	__u32 task_major;
	unsigned int major = 0;
	int gpu_minor = -1;
	__u64 bytes;
	__u32 hclass = 0;
	bool have_hclass = false;
	struct vgpu_ioctl_arg_detail_snapshot detail;
	bool have_detail = false;
	int ret;

	(void)p;
	if (!trace_nvidia_devices)
		return 0;
	if (!vgpu_nv_file_match(file, &gpu_minor, &major))
		return 0;

	known_file = vgpu_device_lookup_file(file, &device_file);
	if (known_file) {
		vgpu_task_touch(current->pid, device_file.tgid,
				device_file.gpu_minor, device_file.nvidia_major);
		task_tgid = device_file.tgid;
		task_gpu_minor = device_file.gpu_minor;
		task_major = device_file.nvidia_major;
	} else {
		vgpu_task_touch(current->pid, current->tgid, gpu_minor, major);
		task_tgid = current->tgid;
		task_gpu_minor = gpu_minor;
		task_major = major;
	}

	vgpu_stats_inc(VGPU_STAT_IOCTL_SEEN);
	vgpu_ioctl_trace_record(cmd, current->pid, task_tgid, task_gpu_minor,
				task_major, arg);
	sample_cmd_count = vgpu_nv_parse_sample_cmds(sample_cmds,
						     ARRAY_SIZE(sample_cmds));
	if (vgpu_ioctl_arg_cmd_match(cmd, sample_cmds, sample_cmd_count))
		vgpu_ioctl_arg_sample_user(cmd, arg, _IOC_SIZE(cmd));
	if (vgpu_ioctl_arg_cmd_match(cmd, sample_cmds, sample_cmd_count) &&
	    ioctl_arg_nested_ptr_offset != UINT_MAX)
		vgpu_ioctl_arg_sample_user_ptr(cmd, arg, _IOC_SIZE(cmd),
					       ioctl_arg_nested_ptr_offset,
					       ioctl_arg_nested_sample_bytes);
	vgpu_events_push(VGPU_EVENT_IOCTL_SEEN, current->pid, current->tgid,
			 gpu_minor, cmd, arg, 0, major);

	if (!vgpu_nv_read_memory_size(arg, &bytes))
		return 0;
	have_hclass = vgpu_nv_read_memory_class(arg, &hclass);
	if (have_hclass) {
		vgpu_ioctl_arg_record_pair(cmd, hclass, bytes);
		have_detail = vgpu_nv_read_alloc_detail(arg, hclass, bytes,
							 &detail);
		if (have_detail)
			vgpu_ioctl_arg_record_detail(cmd, &detail);
	}
	if (memory_alloc_ioctl_cmd != 0 && cmd == memory_alloc_ioctl_cmd &&
	    (have_hclass ? vgpu_nv_memory_class_match(hclass) :
	     memory_alloc_class == 0) &&
	    vgpu_nv_memory_detail_match(have_detail ? &detail : NULL) &&
	    vgpu_nv_memory_size_match(bytes)) {
		ret = vgpu_task_memory_charge(current->pid, task_tgid,
					      task_gpu_minor, task_major, bytes,
					      memory_trace_limit_bytes, true,
					      &would_deny);
		vgpu_stats_inc(VGPU_STAT_MEMORY_ALLOC_SEEN);
		if (would_deny) {
			vgpu_stats_inc(VGPU_STAT_MEMORY_WOULD_DENY);
			vgpu_events_push(VGPU_EVENT_MEMORY_WOULD_DENY,
					 current->pid, task_tgid,
					 task_gpu_minor, bytes,
					 memory_trace_limit_bytes, ret,
					 task_major);
		}
		vgpu_events_push(VGPU_EVENT_MEMORY_ALLOC, current->pid,
				 task_tgid, task_gpu_minor, bytes,
				 memory_trace_limit_bytes, ret, task_major);
	} else if (memory_free_ioctl_cmd != 0 && cmd == memory_free_ioctl_cmd) {
		ret = vgpu_task_memory_uncharge(task_tgid, task_gpu_minor,
						bytes);
		vgpu_stats_inc(VGPU_STAT_MEMORY_FREE_SEEN);
		if (ret == -ENOENT || ret == -ERANGE)
			vgpu_stats_inc(VGPU_STAT_UNMATCHED_FREE);
		vgpu_events_push(VGPU_EVENT_MEMORY_FREE, current->pid,
				 task_tgid, task_gpu_minor, bytes, 0, ret,
				 task_major);
	}
	return 0;
}

static int vgpu_nv_fput_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct file *file = (struct file *)vgpu_arg0(regs);
	struct vgpu_device_file device_file;
	unsigned int major = 0;
	int gpu_minor = -1;

	(void)p;
	if (!trace_nvidia_devices)
		return 0;
	if (!vgpu_nv_file_match(file, &gpu_minor, &major))
		return 0;

	if (vgpu_device_close_file(file, &device_file))
		vgpu_task_close(device_file.tgid, device_file.gpu_minor);

	vgpu_events_push(VGPU_EVENT_NVIDIA_RELEASE, current->pid, current->tgid,
			 gpu_minor, major, 0, 0, major);
	return 0;
}

static struct kprobe vgpu_nv_open_kp = {
	.symbol_name = "chrdev_open",
	.pre_handler = vgpu_nv_chrdev_open_pre,
};

static struct kprobe vgpu_nv_ioctl_kp = {
	.symbol_name = "do_vfs_ioctl",
	.pre_handler = vgpu_nv_ioctl_pre,
};

static struct kprobe vgpu_nv_fput_kp = {
	.symbol_name = "__fput",
	.pre_handler = vgpu_nv_fput_pre,
};

static void vgpu_nv_hook_error(const char *name, int ret)
{
	vgpu_nv_ioctl_status.hook_errors++;
	vgpu_stats_inc(VGPU_STAT_HOOK_ERRORS);
	vgpu_events_push(VGPU_EVENT_HOOK_ERROR, current->pid, current->tgid,
			 -1, 0, 0, ret, 0);
	vgpu_pr_warn("nvidia trace hook %s failed ret=%d\n", name, ret);
}

int vgpu_nv_ioctl_init(void)
{
	int ret;

	memset(&vgpu_nv_ioctl_status, 0, sizeof(vgpu_nv_ioctl_status));
	vgpu_nv_ioctl_status.nvidia_major = nvidia_major;
	vgpu_nv_ioctl_status.nvidia_uvm_major = nvidia_uvm_major;

	ret = register_kprobe(&vgpu_nv_open_kp);
	if (ret)
		vgpu_nv_hook_error("chrdev_open", ret);
	else
		vgpu_nv_ioctl_status.open_hooked = true;

	ret = register_kprobe(&vgpu_nv_ioctl_kp);
	if (ret)
		vgpu_nv_hook_error("do_vfs_ioctl", ret);
	else
		vgpu_nv_ioctl_status.ioctl_hooked = true;

	ret = register_kprobe(&vgpu_nv_fput_kp);
	if (ret)
		vgpu_nv_hook_error("__fput", ret);
	else
		vgpu_nv_ioctl_status.release_hooked = true;

	vgpu_pr_info("nvidia trace hooks open=%u ioctl=%u release=%u major=%u uvm_major=%u errors=%u\n",
		     vgpu_nv_ioctl_status.open_hooked,
		     vgpu_nv_ioctl_status.ioctl_hooked,
		     vgpu_nv_ioctl_status.release_hooked,
		     vgpu_nv_ioctl_status.nvidia_major,
		     vgpu_nv_ioctl_status.nvidia_uvm_major,
		     vgpu_nv_ioctl_status.hook_errors);
	return 0;
}

void vgpu_nv_ioctl_exit(void)
{
	if (vgpu_nv_ioctl_status.release_hooked)
		unregister_kprobe(&vgpu_nv_fput_kp);
	if (vgpu_nv_ioctl_status.ioctl_hooked)
		unregister_kprobe(&vgpu_nv_ioctl_kp);
	if (vgpu_nv_ioctl_status.open_hooked)
		unregister_kprobe(&vgpu_nv_open_kp);

	memset(&vgpu_nv_ioctl_status, 0, sizeof(vgpu_nv_ioctl_status));
}

void vgpu_nv_ioctl_get_status(struct vgpu_nv_ioctl_status *out)
{
	if (!out)
		return;

	vgpu_nv_ioctl_status.memory_trace_enabled =
		(memory_ioctl_arg_is_size ||
		 memory_ioctl_size_offset != UINT_MAX ||
		 (memory_ioctl_nested_ptr_offset != UINT_MAX &&
		  memory_ioctl_nested_size_offset != UINT_MAX)) &&
		(memory_alloc_ioctl_cmd != 0 || memory_free_ioctl_cmd != 0);
	vgpu_nv_ioctl_status.memory_alloc_ioctl_cmd = memory_alloc_ioctl_cmd;
	vgpu_nv_ioctl_status.memory_ioctl_class_offset =
		memory_ioctl_class_offset;
	vgpu_nv_ioctl_status.memory_alloc_class = memory_alloc_class;
	vgpu_nv_ioctl_status.memory_alloc_type = memory_alloc_type;
	vgpu_nv_ioctl_status.memory_alloc_flags = memory_alloc_flags;
	vgpu_nv_ioctl_status.memory_alloc_attr = memory_alloc_attr;
	vgpu_nv_ioctl_status.memory_alloc_attr2 = memory_alloc_attr2;
	vgpu_nv_ioctl_status.memory_alloc_tag = memory_alloc_tag;
	vgpu_nv_ioctl_status.memory_alloc_min_bytes = memory_alloc_min_bytes;
	vgpu_nv_ioctl_status.memory_alloc_max_bytes = memory_alloc_max_bytes;
	vgpu_nv_ioctl_status.memory_free_ioctl_cmd = memory_free_ioctl_cmd;
	vgpu_nv_ioctl_status.memory_ioctl_size_offset = memory_ioctl_size_offset;
	vgpu_nv_ioctl_status.memory_ioctl_nested_ptr_offset =
		memory_ioctl_nested_ptr_offset;
	vgpu_nv_ioctl_status.memory_ioctl_nested_size_offset =
		memory_ioctl_nested_size_offset;
	vgpu_nv_ioctl_status.memory_ioctl_size_filter_value =
		memory_ioctl_size_filter_value;
	vgpu_nv_ioctl_status.memory_ioctl_size_max_bytes =
		memory_ioctl_size_max_bytes;
	vgpu_nv_ioctl_status.memory_ioctl_size_alignment =
		memory_ioctl_size_alignment;
	vgpu_nv_ioctl_status.ioctl_arg_sample_cmd = ioctl_arg_sample_cmd;
	*out = vgpu_nv_ioctl_status;
}
