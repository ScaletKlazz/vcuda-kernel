#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

tasks=/sys/kernel/debug/vgpu/tasks

fail()
{
	printf '\nFAILED: verify-cgroup: %s\n\n' "$1" >&2
	exit 1
}

if ! mount -t cgroup2 | grep -q ' cgroup2 '; then
	fail "cgroup v2 mount not found"
fi

./examples/cuda_malloc_smoke $((64 * 1024 * 1024)) 1 0 >/dev/null

if ! grep -Eq 'cgroup_id=[1-9][0-9]*' "$tasks"; then
	printf '\n' >&2
	cat "$tasks" >&2
	fail "missing non-zero cgroup_id in $tasks"
fi

printf '\nPASS: verify-cgroup\n\n'
cat "$tasks"
printf '\n'
