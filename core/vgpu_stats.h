/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VGPU_STATS_H
#define VGPU_STATS_H

#include "vgpu_types.h"

enum vgpu_stats_counter {
	VGPU_STAT_POLICIES_SET = 0,
	VGPU_STAT_IOCTL_SEEN,
	VGPU_STAT_MEMORY_ALLOC_SEEN,
	VGPU_STAT_MEMORY_FREE_SEEN,
	VGPU_STAT_MEMORY_WOULD_DENY,
	VGPU_STAT_MEMORY_DENIED,
	VGPU_STAT_TIMESLICE_SEEN,
	VGPU_STAT_TIMESLICE_WOULD_REWRITE,
	VGPU_STAT_TIMESLICE_REWRITTEN,
	VGPU_STAT_HOOK_ERRORS,
	VGPU_STAT_EMERGENCY_DISABLED,
	VGPU_STAT_EVENTS_DROPPED,
	VGPU_STAT_UNMATCHED_FREE,
	VGPU_STAT_MAX,
};

void vgpu_stats_init(enum vgpu_runtime_mode mode);
void vgpu_stats_set_mode(enum vgpu_runtime_mode mode);
enum vgpu_runtime_mode vgpu_stats_get_mode(void);
void vgpu_stats_inc(enum vgpu_stats_counter counter);
void vgpu_stats_snapshot(struct vgpu_stats *stats);

#ifdef CONFIG_KUNIT
void vgpu_stats_reset_for_test(enum vgpu_runtime_mode mode);
#endif

#endif /* VGPU_STATS_H */
