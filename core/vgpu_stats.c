// SPDX-License-Identifier: GPL-2.0-only
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "vgpu_stats.h"

static atomic64_t vgpu_counters[VGPU_STAT_MAX];
static atomic_t vgpu_runtime_mode_value;

void vgpu_stats_init(enum vgpu_runtime_mode mode)
{
	int i;

	for (i = 0; i < VGPU_STAT_MAX; i++)
		atomic64_set(&vgpu_counters[i], 0);
	atomic_set(&vgpu_runtime_mode_value, mode);
}

void vgpu_stats_set_mode(enum vgpu_runtime_mode mode)
{
	atomic_set(&vgpu_runtime_mode_value, mode);
}

enum vgpu_runtime_mode vgpu_stats_get_mode(void)
{
	return atomic_read(&vgpu_runtime_mode_value);
}

void vgpu_stats_inc(enum vgpu_stats_counter counter)
{
	if (counter >= VGPU_STAT_MAX)
		return;

	atomic64_inc(&vgpu_counters[counter]);
}

void vgpu_stats_snapshot(struct vgpu_stats *stats)
{
	if (!stats)
		return;

	memset(stats, 0, sizeof(*stats));
	stats->policies_set = atomic64_read(&vgpu_counters[VGPU_STAT_POLICIES_SET]);
	stats->ioctl_seen = atomic64_read(&vgpu_counters[VGPU_STAT_IOCTL_SEEN]);
	stats->memory_alloc_seen =
		atomic64_read(&vgpu_counters[VGPU_STAT_MEMORY_ALLOC_SEEN]);
	stats->memory_free_seen =
		atomic64_read(&vgpu_counters[VGPU_STAT_MEMORY_FREE_SEEN]);
	stats->memory_would_deny =
		atomic64_read(&vgpu_counters[VGPU_STAT_MEMORY_WOULD_DENY]);
	stats->memory_denied =
		atomic64_read(&vgpu_counters[VGPU_STAT_MEMORY_DENIED]);
	stats->timeslice_seen =
		atomic64_read(&vgpu_counters[VGPU_STAT_TIMESLICE_SEEN]);
	stats->timeslice_would_rewrite =
		atomic64_read(&vgpu_counters[VGPU_STAT_TIMESLICE_WOULD_REWRITE]);
	stats->timeslice_rewritten =
		atomic64_read(&vgpu_counters[VGPU_STAT_TIMESLICE_REWRITTEN]);
	stats->hook_errors = atomic64_read(&vgpu_counters[VGPU_STAT_HOOK_ERRORS]);
	stats->emergency_disabled =
		atomic64_read(&vgpu_counters[VGPU_STAT_EMERGENCY_DISABLED]);
	stats->events_dropped =
		atomic64_read(&vgpu_counters[VGPU_STAT_EVENTS_DROPPED]);
	stats->unmatched_free =
		atomic64_read(&vgpu_counters[VGPU_STAT_UNMATCHED_FREE]);
	stats->runtime_mode = atomic_read(&vgpu_runtime_mode_value);
}

#ifdef CONFIG_KUNIT
void vgpu_stats_reset_for_test(enum vgpu_runtime_mode mode)
{
	vgpu_stats_init(mode);
}
#endif
