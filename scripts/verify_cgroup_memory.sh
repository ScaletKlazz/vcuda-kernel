#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

size=${VERIFY_MEMORY_SIZE:-268435456}
loops=${VERIFY_MEMORY_LOOPS:-4}
device=${VERIFY_MEMORY_DEVICE:-0}
gpu_minor=${VERIFY_MEMORY_GPU_MINOR:-255}
limit=${VERIFY_MEMORY_LIMIT:-134217728}

stats=/sys/kernel/debug/vgpu/stats
tasks=/sys/kernel/debug/vgpu/tasks
cgroups=/sys/kernel/debug/vgpu/cgroups

fail()
{
	printf '\nFAILED: verify-cgroup-memory: %s\n\n' "$1" >&2
	exit 1
}

field_value()
{
	name=$1
	file=$2
	tr ' ' '\n' < "$file" | awk -F= -v name="$name" '$1 == name { print $2; exit }'
}

./examples/cuda_malloc_smoke $((64 * 1024 * 1024)) 1 "$device" >/dev/null
cgroup_id=$(sed -n 's/.*cgroup_id=\([0-9][0-9]*\).*/\1/p' "$tasks" | head -n1)
if [ -z "$cgroup_id" ] || [ "$cgroup_id" = "0" ]; then
	printf '\n' >&2
	cat "$tasks" >&2
	fail "missing non-zero cgroup_id in $tasks"
fi

before_would=$(field_value memory_would_deny "$stats")
./examples/vgpu_set_cgroup_policy "$cgroup_id" "$gpu_minor" 100 0x7 "$limit" >/dev/null
./examples/cuda_malloc_smoke "$size" "$loops" "$device"
after_would=$(field_value memory_would_deny "$stats")

if ! grep -q "cgroup_id=$cgroup_id" "$cgroups"; then
	printf '\n' >&2
	cat "$cgroups" >&2
	fail "missing cgroup memory row in $cgroups"
fi

if [ "$after_would" -le "$before_would" ]; then
	printf '\n' >&2
	cat "$cgroups" >&2
	fail "memory_would_deny did not increase: before=$before_would after=$after_would"
fi

printf '\nPASS: verify-cgroup-memory cgroup_id=%s memory_would_deny=%s->%s\n\n' \
	"$cgroup_id" "$before_would" "$after_would"
cat "$cgroups"
printf '\n'
