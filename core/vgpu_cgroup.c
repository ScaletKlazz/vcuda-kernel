// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cgroup.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>

#include "vgpu_cgroup.h"

__u64 vgpu_cgroup_current_id(void)
{
#ifdef CONFIG_CGROUPS
	struct cgroup *cgrp;
	__u64 id = 0;

	rcu_read_lock();
	cgrp = task_dfl_cgroup(current);
	if (cgrp)
		id = cgroup_id(cgrp);
	rcu_read_unlock();

	return id;
#else
	return 0;
#endif
}
