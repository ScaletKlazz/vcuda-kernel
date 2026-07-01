#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

size=${VERIFY_COMPUTE_SIZE:-268435456}
loops=${VERIFY_COMPUTE_LOOPS:-4}
device=${VERIFY_COMPUTE_DEVICE:-0}
sleep_seconds=${VERIFY_COMPUTE_SLEEP:-15}
gpu_minor=${VERIFY_COMPUTE_GPU_MINOR:-255}
weight=${VERIFY_COMPUTE_WEIGHT:-5000}
flags=${VERIFY_COMPUTE_FLAGS:-0x2}

stats=/sys/kernel/debug/vgpu/stats
tasks=/sys/kernel/debug/vgpu/tasks
timeslices=/sys/kernel/debug/vgpu/timeslices

fail()
{
	printf '\nFAILED: verify-cgroup-compute: %s\n\n' "$1" >&2
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

before_seen=$(field_value timeslice_seen "$stats")
before_would=$(field_value timeslice_would_rewrite "$stats")
before_rewritten=$(field_value timeslice_rewritten "$stats")

./examples/vgpu_set_cgroup_policy "$cgroup_id" "$gpu_minor" "$weight" "$flags" >/dev/null
./examples/cuda_malloc_smoke "$size" "$loops" "$device" "$sleep_seconds"

after_seen=$(field_value timeslice_seen "$stats")
after_would=$(field_value timeslice_would_rewrite "$stats")
after_rewritten=$(field_value timeslice_rewritten "$stats")

if [ "$after_seen" -le "$before_seen" ]; then
	fail "timeslice_seen did not increase: before=$before_seen after=$after_seen"
fi

if [ "$after_would" -le "$before_would" ]; then
	fail "timeslice_would_rewrite did not increase: before=$before_would after=$after_would"
fi

if ! grep -q 'policy_scope=2' "$timeslices"; then
	printf '\n' >&2
	tail -n 20 "$timeslices" >&2
	fail "missing cgroup policy_scope=2 in $timeslices"
fi

if grep -q 'mode=enforcing' /sys/kernel/debug/vgpu/enabled && \
   [ "$after_rewritten" -le "$before_rewritten" ]; then
	fail "timeslice_rewritten did not increase in enforcing mode: before=$before_rewritten after=$after_rewritten"
fi

printf '\nPASS: verify-cgroup-compute cgroup_id=%s seen=%s->%s would=%s->%s rewritten=%s->%s\n\n' \
	"$cgroup_id" "$before_seen" "$after_seen" "$before_would" "$after_would" \
	"$before_rewritten" "$after_rewritten"
tail -n 10 "$timeslices"
printf '\n'
