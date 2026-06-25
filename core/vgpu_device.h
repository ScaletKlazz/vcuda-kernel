/* SPDX-License-Identifier: GPL-2.0 */
#ifndef VGPU_DEVICE_H
#define VGPU_DEVICE_H

#include <linux/fs.h>
#include <linux/types.h>

struct vgpu_device_file {
	struct file *file;
	__s32 pid;
	__s32 tgid;
	__s32 gpu_minor;
	__u32 nvidia_major;
};

int vgpu_device_registry_init(void);
void vgpu_device_registry_exit(void);
int vgpu_device_open_file(struct file *file, __s32 pid, __s32 tgid,
			  __s32 gpu_minor, __u32 nvidia_major);
bool vgpu_device_lookup_file(struct file *file, struct vgpu_device_file *out);
bool vgpu_device_close_file(struct file *file, struct vgpu_device_file *out);

#endif /* VGPU_DEVICE_H */
