// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "vgpu_ioctl_arg.h"

static const struct vgpu_ioctl_arg_snapshot *
vgpu_ioctl_arg_test_find(const struct vgpu_ioctl_arg_snapshot *snapshots,
			 size_t count, __u32 offset)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (snapshots[i].offset == offset)
			return &snapshots[i];
	}

	return NULL;
}

static void vgpu_ioctl_arg_test_init(struct kunit *test)
{
	(void)test;
	vgpu_ioctl_arg_reset_for_test();
}

static void vgpu_ioctl_arg_test_captures_u64_offsets(struct kunit *test)
{
	struct vgpu_ioctl_arg_snapshot snapshots[8];
	const struct vgpu_ioctl_arg_snapshot *offset8;
	const struct vgpu_ioctl_arg_snapshot *offset24;
	u8 bytes[32] = { };
	__u64 size = 256ULL * 1024ULL * 1024ULL;
	__u64 marker = 0xfeedULL;
	size_t count;

	memcpy(bytes + 8, &size, sizeof(size));
	memcpy(bytes + 24, &marker, sizeof(marker));

	vgpu_ioctl_arg_record(0xc90046c8, bytes, sizeof(bytes));
	count = vgpu_ioctl_arg_snapshot(snapshots, ARRAY_SIZE(snapshots));

	KUNIT_ASSERT_EQ(test, count, 7UL);
	offset8 = vgpu_ioctl_arg_test_find(snapshots, count, 8);
	offset24 = vgpu_ioctl_arg_test_find(snapshots, count, 24);
	KUNIT_ASSERT_TRUE(test, offset8 != NULL);
	KUNIT_ASSERT_TRUE(test, offset24 != NULL);
	KUNIT_EXPECT_EQ(test, offset8->cmd, 0xc90046c8U);
	KUNIT_EXPECT_EQ(test, offset8->last_u32, 256U * 1024U * 1024U);
	KUNIT_EXPECT_EQ(test, offset8->last_u64, 256ULL * 1024ULL * 1024ULL);
	KUNIT_EXPECT_EQ(test, offset24->last_u64, 0xfeedULL);
}

static void vgpu_ioctl_arg_test_captures_u32_at_four_byte_offset(struct kunit *test)
{
	struct vgpu_ioctl_arg_snapshot snapshots[4];
	const struct vgpu_ioctl_arg_snapshot *offset4;
	u8 bytes[16] = { };
	__u32 size = 256U * 1024U * 1024U;
	size_t count;

	memcpy(bytes + 4, &size, sizeof(size));

	vgpu_ioctl_arg_record(0x1, bytes, sizeof(bytes));
	count = vgpu_ioctl_arg_snapshot(snapshots, ARRAY_SIZE(snapshots));

	KUNIT_ASSERT_EQ(test, count, 3UL);
	offset4 = vgpu_ioctl_arg_test_find(snapshots, count, 4);
	KUNIT_ASSERT_TRUE(test, offset4 != NULL);
	KUNIT_EXPECT_EQ(test, offset4->last_u32, size);
}

static void vgpu_ioctl_arg_test_coalesces_same_cmd_offset(struct kunit *test)
{
	struct vgpu_ioctl_arg_snapshot snapshots[4];
	u8 bytes[16] = { };
	__u64 value = 1;
	size_t count;

	memcpy(bytes, &value, sizeof(value));
	vgpu_ioctl_arg_record(0x1, bytes, sizeof(bytes));
	value = 2;
	memcpy(bytes, &value, sizeof(value));
	vgpu_ioctl_arg_record(0x1, bytes, sizeof(bytes));

	count = vgpu_ioctl_arg_snapshot(snapshots, ARRAY_SIZE(snapshots));

	KUNIT_ASSERT_TRUE(test, count >= 1UL);
	KUNIT_EXPECT_EQ(test, snapshots[0].cmd, 1U);
	KUNIT_EXPECT_EQ(test, snapshots[0].offset, 0U);
	KUNIT_EXPECT_EQ(test, snapshots[0].count, 2ULL);
	KUNIT_EXPECT_EQ(test, snapshots[0].last_u32, 2U);
	KUNIT_EXPECT_EQ(test, snapshots[0].last_u64, 2ULL);
}

static void vgpu_ioctl_arg_test_matches_configured_command_list(struct kunit *test)
{
	static const __u32 cmds[] = {
		0xc00846d6,
		0xc23046d7,
		0xc028465e,
	};

	KUNIT_EXPECT_FALSE(test, vgpu_ioctl_arg_cmd_match(0xc90046c8, cmds,
							  ARRAY_SIZE(cmds)));
	KUNIT_EXPECT_TRUE(test, vgpu_ioctl_arg_cmd_match(0xc23046d7, cmds,
							 ARRAY_SIZE(cmds)));
}

static struct kunit_case vgpu_ioctl_arg_test_cases[] = {
	KUNIT_CASE(vgpu_ioctl_arg_test_captures_u64_offsets),
	KUNIT_CASE(vgpu_ioctl_arg_test_captures_u32_at_four_byte_offset),
	KUNIT_CASE(vgpu_ioctl_arg_test_coalesces_same_cmd_offset),
	KUNIT_CASE(vgpu_ioctl_arg_test_matches_configured_command_list),
	{}
};

static struct kunit_suite vgpu_ioctl_arg_test_suite = {
	.name = "vgpu_ioctl_arg",
	.init = vgpu_ioctl_arg_test_init,
	.test_cases = vgpu_ioctl_arg_test_cases,
};

kunit_test_suite(vgpu_ioctl_arg_test_suite);
