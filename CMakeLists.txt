# SPDX-License-Identifier: GPL-2.0

cmake_minimum_required(VERSION 3.16)

project(vcuda-kernel LANGUAGES NONE)

find_program(MAKE_EXECUTABLE make REQUIRED)
find_program(BASH_EXECUTABLE bash)

set(VCUDA_KERNEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(VCUDA_DRY_RUN "1" CACHE STRING "Load module with dry_run=1 or 0")
set(VCUDA_ALLOW_ENFORCE "0" CACHE STRING "Load module with allow_enforce=1 or 0")

# Mirrors Makefile load defaults. CMake only forwards user policy overrides to
# make; Kbuild remains the real builder. OKM layout values are intentionally not
# hardcoded here: make load extracts them from third_party/open-gpu-kernel-modules
# through scripts/extract_okm_defaults.sh.
set(VCUDA_MEMORY_TRACE "1" CACHE STRING "Enable conservative RM local-memory accounting during make load")
set(VCUDA_MEMORY_ALLOC_MIN_BYTES "16777216" CACHE STRING "Minimum RM local-memory allocation size to account; filters small runtime objects")
set(VCUDA_MEMORY_ALLOC_MAX_BYTES "0" CACHE STRING "Maximum RM local-memory allocation size to account; 0 disables max gate")
set(VCUDA_MEMORY_TRACE_LIMIT_BYTES "0" CACHE STRING "Dry-run memory limit for would-deny events; 0 disables limit")
set(VCUDA_CLEAR_MEMORY_ON_LAST_CLOSE "1" CACHE STRING "Clear task memory accounting when the last NVIDIA fd closes")

# Optional OKM-derived layout overrides. Leave empty to let Makefile extract
# values from OKM. Set only when testing a driver ABI manually.
set(VCUDA_MEMORY_ALLOC_IOCTL_CMD "" CACHE STRING "Optional override for NVIDIA RM alloc ioctl command")
set(VCUDA_MEMORY_IOCTL_CLASS_OFFSET "" CACHE STRING "Optional override for NVOS21_PARAMETERS hClass offset")
set(VCUDA_MEMORY_ALLOC_CLASS "" CACHE STRING "Optional override for local GPU-memory RM class")
set(VCUDA_MEMORY_ALLOC_TYPE "0x00000000" CACHE STRING "NV_MEMORY_ALLOCATION_PARAMS.type gate")
set(VCUDA_MEMORY_ALLOC_FLAGS "0x0001c101" CACHE STRING "NV_MEMORY_ALLOCATION_PARAMS.flags gate observed for CUDA local memory")
set(VCUDA_MEMORY_ALLOC_ATTR "0x18000000" CACHE STRING "NV_MEMORY_ALLOCATION_PARAMS.attr gate observed for CUDA local memory")
set(VCUDA_MEMORY_ALLOC_ATTR2 "0x00000000" CACHE STRING "NV_MEMORY_ALLOCATION_PARAMS.attr2 gate")
set(VCUDA_MEMORY_IOCTL_NESTED_PTR_OFFSET "" CACHE STRING "Optional override for NVOS21_PARAMETERS pAllocParms offset")
set(VCUDA_MEMORY_IOCTL_NESTED_SIZE_OFFSET "" CACHE STRING "Optional override for NV_MEMORY_ALLOCATION_PARAMS.size offset")
set(VCUDA_MEMORY_IOCTL_SIZE_MAX_BYTES "0" CACHE STRING "Raw sampled size upper bound; 0 disables")
set(VCUDA_MEMORY_IOCTL_SIZE_ALIGNMENT "4096" CACHE STRING "Required allocation size alignment for accounting")

set(VCUDA_LOAD_ARGS
  DRY_RUN=${VCUDA_DRY_RUN}
  ALLOW_ENFORCE=${VCUDA_ALLOW_ENFORCE}
  MEMORY_TRACE=${VCUDA_MEMORY_TRACE}
  MEMORY_ALLOC_MIN_BYTES=${VCUDA_MEMORY_ALLOC_MIN_BYTES}
  MEMORY_ALLOC_MAX_BYTES=${VCUDA_MEMORY_ALLOC_MAX_BYTES}
  MEMORY_TRACE_LIMIT_BYTES=${VCUDA_MEMORY_TRACE_LIMIT_BYTES}
  CLEAR_MEMORY_ON_LAST_CLOSE=${VCUDA_CLEAR_MEMORY_ON_LAST_CLOSE}
  MEMORY_ALLOC_TYPE=${VCUDA_MEMORY_ALLOC_TYPE}
  MEMORY_ALLOC_FLAGS=${VCUDA_MEMORY_ALLOC_FLAGS}
  MEMORY_ALLOC_ATTR=${VCUDA_MEMORY_ALLOC_ATTR}
  MEMORY_ALLOC_ATTR2=${VCUDA_MEMORY_ALLOC_ATTR2}
  MEMORY_IOCTL_SIZE_MAX_BYTES=${VCUDA_MEMORY_IOCTL_SIZE_MAX_BYTES}
  MEMORY_IOCTL_SIZE_ALIGNMENT=${VCUDA_MEMORY_IOCTL_SIZE_ALIGNMENT}
)

foreach(optional_arg
    MEMORY_ALLOC_IOCTL_CMD
    MEMORY_IOCTL_CLASS_OFFSET
    MEMORY_ALLOC_CLASS
    MEMORY_IOCTL_NESTED_PTR_OFFSET
    MEMORY_IOCTL_NESTED_SIZE_OFFSET)
  set(cmake_arg "VCUDA_${optional_arg}")
  if(NOT "${${cmake_arg}}" STREQUAL "")
    list(APPEND VCUDA_LOAD_ARGS ${optional_arg}=${${cmake_arg}})
  endif()
endforeach()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(WARNING "vCUDA-kernel builds only on Linux kernel header hosts. Configure is allowed for editing, but build targets need Linux.")
endif()

add_custom_target(kernel-module ALL
  COMMAND "${MAKE_EXECUTABLE}" -C "${VCUDA_KERNEL_SOURCE_DIR}" modules
  WORKING_DIRECTORY "${VCUDA_KERNEL_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Building vgpu-kernel.ko through Kbuild"
)

add_custom_target(kernel-clean
  COMMAND "${MAKE_EXECUTABLE}" -C "${VCUDA_KERNEL_SOURCE_DIR}" clean
  WORKING_DIRECTORY "${VCUDA_KERNEL_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Cleaning Kbuild outputs"
)

add_custom_target(load
  COMMAND "${MAKE_EXECUTABLE}" -C "${VCUDA_KERNEL_SOURCE_DIR}" load
          ${VCUDA_LOAD_ARGS}
  DEPENDS kernel-module
  WORKING_DIRECTORY "${VCUDA_KERNEL_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Loading vgpu-kernel.ko with auto-detected NVIDIA parameters"
)

add_custom_target(unload
  COMMAND "${MAKE_EXECUTABLE}" -C "${VCUDA_KERNEL_SOURCE_DIR}" unload
  WORKING_DIRECTORY "${VCUDA_KERNEL_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Unloading vgpu-kernel.ko"
)

add_custom_target(reload
  COMMAND "${MAKE_EXECUTABLE}" -C "${VCUDA_KERNEL_SOURCE_DIR}" unload
  COMMAND "${MAKE_EXECUTABLE}" -C "${VCUDA_KERNEL_SOURCE_DIR}" load
          ${VCUDA_LOAD_ARGS}
  DEPENDS kernel-module
  WORKING_DIRECTORY "${VCUDA_KERNEL_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Reloading vgpu-kernel.ko"
)

add_custom_target(fingerprint
  COMMAND "${MAKE_EXECUTABLE}" -C "${VCUDA_KERNEL_SOURCE_DIR}" fingerprint
  WORKING_DIRECTORY "${VCUDA_KERNEL_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Showing NVIDIA driver fingerprint from debugfs"
)

if(BASH_EXECUTABLE)
  add_custom_target(debug-hooks
    COMMAND "${BASH_EXECUTABLE}" -lc "cat /sys/kernel/debug/vgpu/hooks"
    USES_TERMINAL
    COMMENT "Showing hook status"
  )

  add_custom_target(debug-tasks
    COMMAND "${BASH_EXECUTABLE}" -lc "cat /sys/kernel/debug/vgpu/tasks"
    USES_TERMINAL
    COMMENT "Showing active GPU task contexts"
  )

  add_custom_target(debug-stats
    COMMAND "${BASH_EXECUTABLE}" -lc "cat /sys/kernel/debug/vgpu/stats"
    USES_TERMINAL
    COMMENT "Showing vCUDA kernel stats"
  )

  add_custom_target(debug-ioctls
    COMMAND "${BASH_EXECUTABLE}" -lc "cat /sys/kernel/debug/vgpu/ioctls"
    USES_TERMINAL
    COMMENT "Showing aggregated NVIDIA ioctl commands"
  )

  add_custom_target(debug-ioctl-args
    COMMAND "${BASH_EXECUTABLE}" -lc "cat /sys/kernel/debug/vgpu/ioctl_args"
    USES_TERMINAL
    COMMENT "Showing sampled ioctl argument u64 fields"
  )

  add_custom_target(debug-events
    COMMAND "${BASH_EXECUTABLE}" -lc "cat /sys/kernel/debug/vgpu/events | tail -n 80"
    USES_TERMINAL
    COMMENT "Showing recent trace events"
  )

  add_custom_target(debug-logs
    COMMAND "${BASH_EXECUTABLE}" -lc "dmesg | tail -n 80"
    USES_TERMINAL
    COMMENT "Showing recent kernel logs"
  )
endif()
