// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/kernel.h>

#include "vgpu_events.h"

static void vgpu_events_test_init(struct kunit *test)
{
	(void)test;
	vgpu_events_reset_for_test();
}

static void vgpu_events_test_type_names(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test,
			    vgpu_event_type_name(VGPU_EVENT_TIMESLICE_REWRITTEN),
			    "TIMESLICE_REWRITTEN");
	KUNIT_EXPECT_STREQ(test, vgpu_event_type_name(999), "UNKNOWN");
}

static void vgpu_events_test_timeslice_reason_names(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test,
			    vgpu_timeslice_reason_name(VGPU_TIMESLICE_REASON_NO_POLICY),
			    "no_policy");
	KUNIT_EXPECT_STREQ(test,
			    vgpu_timeslice_reason_name(VGPU_TIMESLICE_REASON_CLAMPED_MIN),
			    "clamped_min");
	KUNIT_EXPECT_STREQ(test, vgpu_timeslice_reason_name(999), "unknown");
}

static void vgpu_events_test_timeslice_trace_snapshot(struct kunit *test)
{
	struct vgpu_timeslice_record records[2];
	size_t count;

	vgpu_timeslice_trace_push(VGPU_EVENT_TIMESLICE_WOULD_REWRITE,
				   100, 100, 255, 2000, 1000, 0, 5000,
				   VGPU_TIMESLICE_POLICY_SCOPE_TGID,
				   VGPU_TIMESLICE_REASON_CLAMPED_MIN, 0, 195);

	count = vgpu_timeslice_trace_snapshot(records, ARRAY_SIZE(records));

	KUNIT_ASSERT_EQ(test, count, 1UL);
	KUNIT_EXPECT_EQ(test, records[0].type,
			VGPU_EVENT_TIMESLICE_WOULD_REWRITE);
	KUNIT_EXPECT_EQ(test, records[0].pid, 100);
	KUNIT_EXPECT_EQ(test, records[0].tgid, 100);
	KUNIT_EXPECT_EQ(test, records[0].gpu_minor, 255);
	KUNIT_EXPECT_EQ(test, records[0].old_timeslice_us, 2000ULL);
	KUNIT_EXPECT_EQ(test, records[0].new_timeslice_us, 1000ULL);
	KUNIT_EXPECT_EQ(test, records[0].cgroup_id, 0ULL);
	KUNIT_EXPECT_EQ(test, records[0].weight, 5000U);
	KUNIT_EXPECT_EQ(test, records[0].policy_scope,
			 VGPU_TIMESLICE_POLICY_SCOPE_TGID);
	KUNIT_EXPECT_EQ(test, records[0].reason,
			VGPU_TIMESLICE_REASON_CLAMPED_MIN);
	KUNIT_EXPECT_EQ(test, records[0].error, 0);
	KUNIT_EXPECT_EQ(test, records[0].flags, 195U);
}

static struct kunit_case vgpu_events_test_cases[] = {
	KUNIT_CASE(vgpu_events_test_type_names),
	KUNIT_CASE(vgpu_events_test_timeslice_reason_names),
	KUNIT_CASE(vgpu_events_test_timeslice_trace_snapshot),
	{}
};

static struct kunit_suite vgpu_events_test_suite = {
	.name = "vgpu_events",
	.init = vgpu_events_test_init,
	.test_cases = vgpu_events_test_cases,
};

kunit_test_suite(vgpu_events_test_suite);
