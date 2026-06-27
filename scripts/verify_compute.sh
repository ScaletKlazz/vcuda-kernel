#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-2.0-only
# Verify compute timeslice dry-run or enforcement through debugfs evidence.

set -eu

size=${VERIFY_COMPUTE_SIZE:-268435456}
loops=${VERIFY_COMPUTE_LOOPS:-4}
device=${VERIFY_COMPUTE_DEVICE:-0}
sleep_seconds=${VERIFY_COMPUTE_SLEEP:-15}
gpu_minor=${VERIFY_COMPUTE_GPU_MINOR:-255}
weight=${VERIFY_COMPUTE_WEIGHT:-5000}
flags=${VERIFY_COMPUTE_FLAGS:-0x2}

stats=/sys/kernel/debug/vgpu/stats
timeslices=/sys/kernel/debug/vgpu/timeslices

require_file()
{
	if [ ! -r "$1" ]; then
		printf 'missing readable file: %s\n' "$1" >&2
		exit 1
	fi
}

field_value()
{
	name=$1
	file=$2
	tr ' ' '\n' < "$file" | awk -F= -v name="$name" '$1 == name { print $2; exit }'
}

require_file "$stats"
require_file "$timeslices"

before_seen=$(field_value timeslice_seen "$stats")
before_would=$(field_value timeslice_would_rewrite "$stats")
before_rewritten=$(field_value timeslice_rewritten "$stats")

./examples/cuda_malloc_smoke "$size" "$loops" "$device" "$sleep_seconds" &
pid=$!
./examples/vgpu_set_policy "$pid" "$gpu_minor" "$weight" "$flags"
wait "$pid"

after_seen=$(field_value timeslice_seen "$stats")
after_would=$(field_value timeslice_would_rewrite "$stats")
after_rewritten=$(field_value timeslice_rewritten "$stats")

if [ "$after_seen" -le "$before_seen" ]; then
	printf 'timeslice_seen did not increase: before=%s after=%s\n' "$before_seen" "$after_seen" >&2
	exit 1
fi

if [ "$after_would" -le "$before_would" ]; then
	printf 'timeslice_would_rewrite did not increase: before=%s after=%s\n' "$before_would" "$after_would" >&2
	exit 1
fi

if grep -q 'mode=enforcing' /sys/kernel/debug/vgpu/enabled; then
	if [ "$after_rewritten" -le "$before_rewritten" ]; then
		printf 'timeslice_rewritten did not increase in enforcing mode: before=%s after=%s\n' "$before_rewritten" "$after_rewritten" >&2
		exit 1
	fi
	grep -q 'name=TIMESLICE_REWRITTEN' "$timeslices" || {
		printf 'missing TIMESLICE_REWRITTEN in %s\n' "$timeslices" >&2
		exit 1
	}
else
	grep -q 'name=TIMESLICE_WOULD_REWRITE' "$timeslices" || {
		printf 'missing TIMESLICE_WOULD_REWRITE in %s\n' "$timeslices" >&2
		exit 1
	}
fi

printf 'verify-compute ok seen=%s->%s would=%s->%s rewritten=%s->%s\n' \
	"$before_seen" "$after_seen" "$before_would" "$after_would" \
	"$before_rewritten" "$after_rewritten"
tail -n 10 "$timeslices"
