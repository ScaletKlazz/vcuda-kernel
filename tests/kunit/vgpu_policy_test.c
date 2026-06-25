// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>

#include "vgpu_policy.h"

static struct vgpu_policy vgpu_policy_test_policy(__s32 tgid,
						  __s32 gpu_minor)
{
	return (struct vgpu_policy) {
		.tgid = tgid,
		.gpu_minor = gpu_minor,
		.memory_limit_bytes = 1024 * 1024,
		.compute_weight = 100,
		.flags = VGPU_POLICY_F_MEMORY | VGPU_POLICY_F_COMPUTE |
			 VGPU_POLICY_F_DRY_RUN,
	};
}

static void vgpu_policy_test_insert_and_get(struct kunit *test)
{
	struct vgpu_policy_table table;
	struct vgpu_policy policy = vgpu_policy_test_policy(1234, 0);
	struct vgpu_policy found = { };

	KUNIT_ASSERT_EQ(test, vgpu_policy_table_init(&table), 0);
	KUNIT_EXPECT_EQ(test, vgpu_policy_set(&table, &policy), 0);
	KUNIT_EXPECT_EQ(test, vgpu_policy_get(&table, 1234, 0, &found), 0);
	KUNIT_EXPECT_EQ(test, found.tgid, policy.tgid);
	KUNIT_EXPECT_EQ(test, found.gpu_minor, policy.gpu_minor);
	KUNIT_EXPECT_EQ(test, found.memory_limit_bytes,
			 policy.memory_limit_bytes);
	KUNIT_EXPECT_EQ(test, found.compute_weight, policy.compute_weight);
	KUNIT_EXPECT_EQ(test, found.flags, policy.flags);
	vgpu_policy_table_destroy(&table);
}

static void vgpu_policy_test_replace_existing(struct kunit *test)
{
	struct vgpu_policy_table table;
	struct vgpu_policy policy = vgpu_policy_test_policy(1234, 0);
	struct vgpu_policy replacement = vgpu_policy_test_policy(1234, 0);
	struct vgpu_policy found = { };

	replacement.memory_limit_bytes = 2 * 1024 * 1024;
	replacement.compute_weight = 250;
	replacement.flags = VGPU_POLICY_F_MEMORY | VGPU_POLICY_F_COMPUTE;

	KUNIT_ASSERT_EQ(test, vgpu_policy_table_init(&table), 0);
	KUNIT_EXPECT_EQ(test, vgpu_policy_set(&table, &policy), 0);
	KUNIT_EXPECT_EQ(test, vgpu_policy_set(&table, &replacement), 0);
	KUNIT_EXPECT_EQ(test, vgpu_policy_get(&table, 1234, 0, &found), 0);
	KUNIT_EXPECT_EQ(test, found.memory_limit_bytes,
			 replacement.memory_limit_bytes);
	KUNIT_EXPECT_EQ(test, found.compute_weight, replacement.compute_weight);
	KUNIT_EXPECT_EQ(test, found.flags, replacement.flags);
	vgpu_policy_table_destroy(&table);
}

static void vgpu_policy_test_lookup_miss(struct kunit *test)
{
	struct vgpu_policy_table table;
	struct vgpu_policy found = { };

	KUNIT_ASSERT_EQ(test, vgpu_policy_table_init(&table), 0);
	KUNIT_EXPECT_EQ(test, vgpu_policy_get(&table, 5678, 0, &found), -ENOENT);
	vgpu_policy_table_destroy(&table);
}

static void vgpu_policy_test_rejects_invalid_weight(struct kunit *test)
{
	struct vgpu_policy policy = vgpu_policy_test_policy(1234, 0);

	policy.compute_weight = 0;
	KUNIT_EXPECT_EQ(test, vgpu_policy_validate(&policy), -EINVAL);

	policy.compute_weight = 10001;
	KUNIT_EXPECT_EQ(test, vgpu_policy_validate(&policy), -EINVAL);
}

static void vgpu_policy_test_rejects_invalid_memory_limit(struct kunit *test)
{
	struct vgpu_policy policy = vgpu_policy_test_policy(1234, 0);

	policy.memory_limit_bytes = 0;
	policy.flags = VGPU_POLICY_F_MEMORY;
	KUNIT_EXPECT_EQ(test, vgpu_policy_validate(&policy), -EINVAL);

	policy.flags = VGPU_POLICY_F_COMPUTE;
	KUNIT_EXPECT_EQ(test, vgpu_policy_validate(&policy), 0);
}

static void vgpu_policy_test_preserves_dry_run_flag(struct kunit *test)
{
	struct vgpu_policy_table table;
	struct vgpu_policy policy = vgpu_policy_test_policy(1234, 0);
	struct vgpu_policy found = { };

	KUNIT_ASSERT_EQ(test, vgpu_policy_table_init(&table), 0);
	KUNIT_EXPECT_EQ(test, vgpu_policy_set(&table, &policy), 0);
	KUNIT_EXPECT_EQ(test, vgpu_policy_get(&table, 1234, 0, &found), 0);
	KUNIT_EXPECT_TRUE(test, found.flags & VGPU_POLICY_F_DRY_RUN);
	vgpu_policy_table_destroy(&table);
}

static struct kunit_case vgpu_policy_test_cases[] = {
	KUNIT_CASE(vgpu_policy_test_insert_and_get),
	KUNIT_CASE(vgpu_policy_test_replace_existing),
	KUNIT_CASE(vgpu_policy_test_lookup_miss),
	KUNIT_CASE(vgpu_policy_test_rejects_invalid_weight),
	KUNIT_CASE(vgpu_policy_test_rejects_invalid_memory_limit),
	KUNIT_CASE(vgpu_policy_test_preserves_dry_run_flag),
	{}
};

static struct kunit_suite vgpu_policy_test_suite = {
	.name = "vgpu_policy",
	.test_cases = vgpu_policy_test_cases,
};

kunit_test_suite(vgpu_policy_test_suite);
