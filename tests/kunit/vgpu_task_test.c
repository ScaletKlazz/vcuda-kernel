// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>
#include <linux/errno.h>

#include "vgpu_task.h"

struct vgpu_task_test_find {
	__s32 tgid;
	__s32 gpu_minor;
	struct vgpu_task_snapshot snapshot;
	bool found;
};

static int vgpu_task_test_capture(const struct vgpu_task_snapshot *task,
				  void *data)
{
	struct vgpu_task_test_find *find = data;

	if (task->tgid == find->tgid && task->gpu_minor == find->gpu_minor) {
		find->snapshot = *task;
		find->found = true;
	}

	return 0;
}

static bool vgpu_task_test_snapshot(struct kunit *test, __s32 tgid,
				    __s32 gpu_minor,
				    struct vgpu_task_snapshot *snapshot)
{
	struct vgpu_task_test_find find = {
		.tgid = tgid,
		.gpu_minor = gpu_minor,
	};

	KUNIT_EXPECT_EQ(test, vgpu_task_for_each(vgpu_task_test_capture, &find),
			0);
	KUNIT_EXPECT_TRUE(test, find.found);
	if (find.found && snapshot)
		*snapshot = find.snapshot;
	return find.found;
}

static bool vgpu_task_test_exists(struct kunit *test, __s32 tgid,
				  __s32 gpu_minor)
{
	struct vgpu_task_test_find find = {
		.tgid = tgid,
		.gpu_minor = gpu_minor,
	};

	KUNIT_EXPECT_EQ(test, vgpu_task_for_each(vgpu_task_test_capture, &find),
			0);
	return find.found;
}

static void vgpu_task_test_init(struct kunit *test)
{
	(void)test;
	vgpu_task_reset_for_test();
}

static void vgpu_task_test_charge_within_limit(struct kunit *test)
{
	bool would_deny = true;
	struct vgpu_task_snapshot snapshot;

	KUNIT_EXPECT_EQ(test,
			vgpu_task_memory_charge(100, 100, 0, 195, 512, 1024,
						false, &would_deny),
			0);
	KUNIT_EXPECT_FALSE(test, would_deny);

	KUNIT_ASSERT_TRUE(test, vgpu_task_test_snapshot(test, 100, 0,
							&snapshot));
	KUNIT_EXPECT_EQ(test, snapshot.memory_used_bytes, 512ULL);
}

static void vgpu_task_test_dry_run_over_limit_charges(struct kunit *test)
{
	bool would_deny = false;
	struct vgpu_task_snapshot snapshot;

	KUNIT_EXPECT_EQ(test,
			vgpu_task_memory_charge(100, 100, 0, 195, 2048, 1024,
						true, &would_deny),
			1);
	KUNIT_EXPECT_TRUE(test, would_deny);

	KUNIT_ASSERT_TRUE(test, vgpu_task_test_snapshot(test, 100, 0,
							&snapshot));
	KUNIT_EXPECT_EQ(test, snapshot.memory_used_bytes, 2048ULL);
}

static void vgpu_task_test_enforce_over_limit_denies(struct kunit *test)
{
	bool would_deny = false;
	struct vgpu_task_snapshot snapshot;

	KUNIT_EXPECT_EQ(test,
			vgpu_task_memory_charge(100, 100, 0, 195, 512, 1024,
						false, &would_deny),
			0);
	KUNIT_EXPECT_EQ(test,
			vgpu_task_memory_charge(100, 100, 0, 195, 1024, 1024,
						false, &would_deny),
			-EDQUOT);
	KUNIT_EXPECT_TRUE(test, would_deny);

	KUNIT_ASSERT_TRUE(test, vgpu_task_test_snapshot(test, 100, 0,
							&snapshot));
	KUNIT_EXPECT_EQ(test, snapshot.memory_used_bytes, 512ULL);
}

static void vgpu_task_test_uncharge_clamps_underflow(struct kunit *test)
{
	struct vgpu_task_snapshot snapshot;

	KUNIT_EXPECT_EQ(test,
			vgpu_task_memory_charge(100, 100, 0, 195, 512, 1024,
						false, NULL),
			0);
	KUNIT_EXPECT_EQ(test, vgpu_task_memory_uncharge(100, 0, 1024), -ERANGE);

	KUNIT_ASSERT_TRUE(test, vgpu_task_test_snapshot(test, 100, 0,
							&snapshot));
	KUNIT_EXPECT_EQ(test, snapshot.memory_used_bytes, 0ULL);
}

static void vgpu_task_test_charge_rejects_overflow(struct kunit *test)
{
	bool would_deny = false;
	struct vgpu_task_snapshot snapshot;

	KUNIT_EXPECT_EQ(test,
			vgpu_task_memory_charge(100, 100, 0, 195, ~(__u64)0,
						0, true, NULL),
			0);
	KUNIT_EXPECT_EQ(test,
			vgpu_task_memory_charge(100, 100, 0, 195, 1, 0, true,
						&would_deny),
			-EOVERFLOW);
	KUNIT_EXPECT_TRUE(test, would_deny);

	KUNIT_ASSERT_TRUE(test, vgpu_task_test_snapshot(test, 100, 0,
							&snapshot));
	KUNIT_EXPECT_EQ(test, snapshot.memory_used_bytes, ~(__u64)0);
}

static void vgpu_task_test_close_clears_memory_on_last_fd(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, vgpu_task_open(100, 100, 0, 195), 0);
	KUNIT_EXPECT_EQ(test,
			vgpu_task_memory_charge(100, 100, 0, 195, 512, 0, true,
						NULL),
			0);

	vgpu_task_close(100, 0);

	KUNIT_EXPECT_FALSE(test, vgpu_task_test_exists(test, 100, 0));
}

static struct kunit_case vgpu_task_test_cases[] = {
	KUNIT_CASE(vgpu_task_test_charge_within_limit),
	KUNIT_CASE(vgpu_task_test_dry_run_over_limit_charges),
	KUNIT_CASE(vgpu_task_test_enforce_over_limit_denies),
	KUNIT_CASE(vgpu_task_test_uncharge_clamps_underflow),
	KUNIT_CASE(vgpu_task_test_charge_rejects_overflow),
	KUNIT_CASE(vgpu_task_test_close_clears_memory_on_last_fd),
	{}
};

static struct kunit_suite vgpu_task_test_suite = {
	.name = "vgpu_task",
	.init = vgpu_task_test_init,
	.test_cases = vgpu_task_test_cases,
};

kunit_test_suite(vgpu_task_test_suite);
