#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-2.0-only
# Extract NVIDIA 570 OKM RM allocation layout defaults for vCUDA-kernel.
# Output is shell syntax consumed by Makefile load target.

set -eu

root=${1:-third_party/open-gpu-kernel-modules}
nv_escape="$root/src/nvidia/arch/nvalloc/unix/include/nv_escape.h"
nvos="$root/src/common/sdk/nvidia/inc/nvos.h"
cl0040="$root/src/common/sdk/nvidia/inc/class/cl0040.h"

die()
{
	printf '%s\n' "$*" >&2
	exit 1
}

[ -r "$nv_escape" ] || die "missing OKM file: $nv_escape"
[ -r "$nvos" ] || die "missing OKM file: $nvos"
[ -r "$cl0040" ] || die "missing OKM file: $cl0040"

macro_value()
{
	name=$1
	file=$2
	awk -v name="$name" '
		$1 == "#define" && $2 == name {
			value = $3
			gsub(/[()UuLl]/, "", value)
			print value
			exit
		}
	' "$file"
}

field_offset()
{
	struct_name=$1
	field_name=$2
	file=$3
	awk -v struct_name="$struct_name" -v field_name="$field_name" '
		function align_to(value, align) {
			return int((value + align - 1) / align) * align
		}
		function field_size(type) {
			if (type == "NvP64" || type == "NvU64") return 8
			return 4
		}
		function field_align(type) {
			if (type == "NvP64" || type == "NvU64") return 8
			return 4
		}
		in_struct && $0 ~ "} " struct_name ";" {
			if (candidate != "") print candidate
			exit
		}
		in_struct && $0 ~ /^}/ { in_struct = 0; candidate = "" }
		in_struct && $1 ~ /^Nv/ {
			type = $1
			name = $2
			gsub(/[;\[]/, "", name)
			offset = align_to(offset, field_align(type))
			if (name == field_name) {
				candidate = offset
			}
			offset += field_size(type)
		}
		$0 ~ "typedef struct" { maybe_struct = 1; offset = 0; candidate = "" }
		maybe_struct && $0 ~ "{" { in_struct = 1; maybe_struct = 0; offset = 0; candidate = "" }
	' "$file"
}

rm_alloc=$(macro_value NV_ESC_RM_ALLOC "$nv_escape")
local_user=$(macro_value NV01_MEMORY_LOCAL_USER "$cl0040")
hclass_offset=$(field_offset NVOS21_PARAMETERS hClass "$nvos")
alloc_ptr_offset=$(field_offset NVOS21_PARAMETERS pAllocParms "$nvos")
size_offset=$(field_offset NV_MEMORY_ALLOCATION_PARAMS size "$nvos")

[ -n "$rm_alloc" ] || die "failed to extract NV_ESC_RM_ALLOC"
[ -n "$local_user" ] || die "failed to extract NV01_MEMORY_LOCAL_USER"
[ -n "$hclass_offset" ] || die "failed to extract NVOS21_PARAMETERS.hClass offset"
[ -n "$alloc_ptr_offset" ] || die "failed to extract NVOS21_PARAMETERS.pAllocParms offset"
[ -n "$size_offset" ] || die "failed to extract NV_MEMORY_ALLOCATION_PARAMS.size offset"

# Linux _IOC(_IOC_READ|_IOC_WRITE, 'F', NV_ESC_RM_ALLOC, 0x30).
# The 0x30 RM ioctl wrapper size is observed for NVIDIA 570 Linux RM alloc ABI.
rm_alloc_dec=$((rm_alloc))
ioctl_cmd=$(( (3 << 30) | (0x30 << 16) | (0x46 << 8) | rm_alloc_dec ))

printf 'OKM_DEFAULTS_AVAILABLE=1\n'
printf 'MEMORY_ALLOC_IOCTL_CMD=0x%08x\n' "$ioctl_cmd"
printf 'MEMORY_IOCTL_CLASS_OFFSET=%s\n' "$hclass_offset"
printf 'MEMORY_ALLOC_CLASS=%s\n' "$local_user"
printf 'MEMORY_ALLOC_TYPE=0x00000000\n'
printf 'MEMORY_IOCTL_NESTED_PTR_OFFSET=%s\n' "$alloc_ptr_offset"
printf 'MEMORY_IOCTL_NESTED_SIZE_OFFSET=%s\n' "$size_offset"
