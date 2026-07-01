#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

tasks=/sys/kernel/debug/vgpu/tasks
policies=/sys/kernel/debug/vgpu/cgroup_policies

fail()
{
	printf '\nFAILED: verify-cgroup-policy: %s\n\n' "$1" >&2
	exit 1
}

./examples/cuda_malloc_smoke $((64 * 1024 * 1024)) 1 0 >/dev/null
cgroup_id=$(sed -n 's/.*cgroup_id=\([0-9][0-9]*\).*/\1/p' "$tasks" | head -n1)
if [ -z "$cgroup_id" ] || [ "$cgroup_id" = "0" ]; then
	printf '\n' >&2
	cat "$tasks" >&2
	fail "missing non-zero cgroup_id in $tasks"
fi

./examples/vgpu_set_cgroup_policy "$cgroup_id" 255 100 0x6 1 >/dev/null
if ! grep -q "cgroup_id=$cgroup_id" "$policies"; then
	printf '\n' >&2
	cat "$policies" >&2
	fail "missing cgroup policy in $policies"
fi

printf '\nPASS: verify-cgroup-policy cgroup_id=%s\n\n' "$cgroup_id"
cat "$policies"
printf '\n'
