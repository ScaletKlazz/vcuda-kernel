// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/kernel.h>

#include "vgpu_ioctl_trace.h"

static void vgpu_ioctl_trace_test_init(struct kunit *test)
{
	(void)test;
	vgpu_ioctl_trace_reset_for_test();
}

static void vgpu_ioctl_trace_test_coalesces_command(struct kunit *test)
{
	struct vgpu_ioctl_trace_snapshot snapshots[4];
	size_t count;

	vgpu_ioctl_trace_record(0xc020462a, 100, 100, 0, 195, 4096);
	vgpu_ioctl_trace_record(0xc020462a, 101, 100, 0, 195, 8192);

	count = vgpu_ioctl_trace_snapshot(snapshots, ARRAY_SIZE(snapshots));

	KUNIT_ASSERT_EQ(test, count, 1UL);
	KUNIT_EXPECT_EQ(test, snapshots[0].cmd, 0xc020462aU);
	KUNIT_EXPECT_EQ(test, snapshots[0].count, 2ULL);
	KUNIT_EXPECT_EQ(test, snapshots[0].last_arg, 8192ULL);
	KUNIT_EXPECT_EQ(test, snapshots[0].last_pid, 101);
	KUNIT_EXPECT_EQ(test, snapshots[0].last_tgid, 100);
	KUNIT_EXPECT_EQ(test, snapshots[0].last_gpu_minor, 0);
	KUNIT_EXPECT_EQ(test, snapshots[0].last_nvidia_major, 195U);
}

static void vgpu_ioctl_trace_test_snapshots_distinct_commands(struct kunit *test)
{
	struct vgpu_ioctl_trace_snapshot snapshots[4];
	size_t count;

	vgpu_ioctl_trace_record(0x1, 100, 100, 0, 195, 11);
	vgpu_ioctl_trace_record(0x2, 101, 101, 1, 195, 22);

	count = vgpu_ioctl_trace_snapshot(snapshots, ARRAY_SIZE(snapshots));

	KUNIT_EXPECT_EQ(test, count, 2UL);
}

static struct kunit_case vgpu_ioctl_trace_test_cases[] = {
	KUNIT_CASE(vgpu_ioctl_trace_test_coalesces_command),
	KUNIT_CASE(vgpu_ioctl_trace_test_snapshots_distinct_commands),
	{}
};

static struct kunit_suite vgpu_ioctl_trace_test_suite = {
	.name = "vgpu_ioctl_trace",
	.init = vgpu_ioctl_trace_test_init,
	.test_cases = vgpu_ioctl_trace_test_cases,
};

kunit_test_suite(vgpu_ioctl_trace_test_suite);
