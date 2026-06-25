/* SPDX-License-Identifier: GPL-2.0 */
#ifndef VGPU_TRACE_H
#define VGPU_TRACE_H

#include <linux/printk.h>

#define vgpu_pr_info(fmt, ...) \
	pr_info("vgpu: " fmt, ##__VA_ARGS__)

#define vgpu_pr_warn(fmt, ...) \
	pr_warn("vgpu: " fmt, ##__VA_ARGS__)

#define vgpu_pr_err(fmt, ...) \
	pr_err("vgpu: " fmt, ##__VA_ARGS__)

#endif /* VGPU_TRACE_H */
