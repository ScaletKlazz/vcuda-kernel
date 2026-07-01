// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/errno.h>

#include "vgpu_cgroup_mem.h"

struct vgpu_cgroup_mem_test_find {
	__u64 cgroup_id;
	__s32 gpu_minor;
	struct vgpu_cgroup_mem_snapshot snapshot;
	bool found;
};

static int vgpu_cgroup_mem_test_capture(
	const struct vgpu_cgroup_mem_snapshot *snapshot, void *data)
{
	struct vgpu_cgroup_mem_test_find *find = data;

	if (snapshot->cgroup_id == find->cgroup_id &&
	    snapshot->gpu_minor == find->gpu_minor) {
		find->snapshot = *snapshot;
		find->found = true;
	}

	return 0;
}

static bool vgpu_cgroup_mem_test_snapshot(
	struct kunit *test, __u64 cgroup_id, __s32 gpu_minor,
	struct vgpu_cgroup_mem_snapshot *snapshot)
{
	struct vgpu_cgroup_mem_test_find find = {
		.cgroup_id = cgroup_id,
		.gpu_minor = gpu_minor,
	};

	KUNIT_EXPECT_EQ(test,
			vgpu_cgroup_mem_for_each(vgpu_cgroup_mem_test_capture,
							 &find),
			0);
	KUNIT_EXPECT_TRUE(test, find.found);
	if (find.found && snapshot)
		*snapshot = find.snapshot;
	return find.found;
}

static void vgpu_cgroup_mem_test_init(struct kunit *test)
{
	(void)test;
	vgpu_cgroup_mem_reset_for_test();
}

static void vgpu_cgroup_mem_test_charge_within_limit(struct kunit *test)
{
	struct vgpu_cgroup_mem_snapshot snapshot;
	bool would_deny = true;

	KUNIT_EXPECT_EQ(test,
			vgpu_cgroup_mem_charge(42, 0, 512, 1024, false,
						 &would_deny),
			0);
	KUNIT_EXPECT_FALSE(test, would_deny);
	KUNIT_ASSERT_TRUE(test,
			  vgpu_cgroup_mem_test_snapshot(test, 42, 0, &snapshot));
	KUNIT_EXPECT_EQ(test, snapshot.memory_used_bytes, 512ULL);
	KUNIT_EXPECT_EQ(test, snapshot.alloc_seen, 1ULL);
}

static void vgpu_cgroup_mem_test_dry_run_over_limit_charges(struct kunit *test)
{
	struct vgpu_cgroup_mem_snapshot snapshot;
	bool would_deny = false;

	KUNIT_EXPECT_EQ(test,
			vgpu_cgroup_mem_charge(42, 0, 2048, 1024, true,
						 &would_deny),
			1);
	KUNIT_EXPECT_TRUE(test, would_deny);
	KUNIT_ASSERT_TRUE(test,
			  vgpu_cgroup_mem_test_snapshot(test, 42, 0, &snapshot));
	KUNIT_EXPECT_EQ(test, snapshot.memory_used_bytes, 2048ULL);
	KUNIT_EXPECT_EQ(test, snapshot.would_deny, 1ULL);
}

static void vgpu_cgroup_mem_test_enforce_over_limit_denies(struct kunit *test)
{
	struct vgpu_cgroup_mem_snapshot snapshot;
	bool would_deny = false;

	KUNIT_EXPECT_EQ(test,
			vgpu_cgroup_mem_charge(42, 0, 2048, 1024, false,
						 &would_deny),
			-EDQUOT);
	KUNIT_EXPECT_TRUE(test, would_deny);
	KUNIT_ASSERT_TRUE(test,
			  vgpu_cgroup_mem_test_snapshot(test, 42, 0, &snapshot));
	KUNIT_EXPECT_EQ(test, snapshot.memory_used_bytes, 0ULL);
	KUNIT_EXPECT_EQ(test, snapshot.denied, 1ULL);
}

static void vgpu_cgroup_mem_test_object_replace_and_free(struct kunit *test)
{
	struct vgpu_cgroup_mem_snapshot snapshot;
	__u64 freed = 0;

	KUNIT_EXPECT_EQ(test,
			vgpu_cgroup_mem_charge_object(42, 0, 0x100, 512, 0, true,
							NULL),
			0);
	KUNIT_EXPECT_EQ(test,
			vgpu_cgroup_mem_charge_object(42, 0, 0x100, 1024, 0,
							true, NULL),
			0);
	KUNIT_ASSERT_TRUE(test,
			  vgpu_cgroup_mem_test_snapshot(test, 42, 0, &snapshot));
	KUNIT_EXPECT_EQ(test, snapshot.memory_used_bytes, 1024ULL);

	KUNIT_EXPECT_EQ(test,
			vgpu_cgroup_mem_uncharge_object(42, 0, 0x100, &freed),
			0);
	KUNIT_EXPECT_EQ(test, freed, 1024ULL);
	KUNIT_ASSERT_TRUE(test,
			  vgpu_cgroup_mem_test_snapshot(test, 42, 0, &snapshot));
	KUNIT_EXPECT_EQ(test, snapshot.memory_used_bytes, 0ULL);
	KUNIT_EXPECT_EQ(test, snapshot.free_seen, 1ULL);
}

static void vgpu_cgroup_mem_test_rejects_overflow(struct kunit *test)
{
	bool would_deny = false;

	KUNIT_EXPECT_EQ(test,
			vgpu_cgroup_mem_charge(42, 0, ~(__u64)0, 0, true, NULL),
			0);
	KUNIT_EXPECT_EQ(test,
			vgpu_cgroup_mem_charge(42, 0, 1, 0, true, &would_deny),
			-EOVERFLOW);
	KUNIT_EXPECT_TRUE(test, would_deny);
}

static struct kunit_case vgpu_cgroup_mem_test_cases[] = {
	KUNIT_CASE(vgpu_cgroup_mem_test_charge_within_limit),
	KUNIT_CASE(vgpu_cgroup_mem_test_dry_run_over_limit_charges),
	KUNIT_CASE(vgpu_cgroup_mem_test_enforce_over_limit_denies),
	KUNIT_CASE(vgpu_cgroup_mem_test_object_replace_and_free),
	KUNIT_CASE(vgpu_cgroup_mem_test_rejects_overflow),
	{}
};

static struct kunit_suite vgpu_cgroup_mem_test_suite = {
	.name = "vgpu_cgroup_mem",
	.init = vgpu_cgroup_mem_test_init,
	.test_cases = vgpu_cgroup_mem_test_cases,
};

kunit_test_suite(vgpu_cgroup_mem_test_suite);
