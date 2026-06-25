# SPDX-License-Identifier: GPL-2.0

cmake_minimum_required(VERSION 3.16)

project(vcuda-kernel LANGUAGES NONE)

find_program(MAKE_EXECUTABLE make REQUIRED)
find_program(BASH_EXECUTABLE bash)

set(VCUDA_KERNEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(VCUDA_DRY_RUN "1" CACHE STRING "Load module with dry_run=1 or 0")
set(VCUDA_ALLOW_ENFORCE "0" CACHE STRING "Load module with allow_enforce=1 or 0")

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
          DRY_RUN="${VCUDA_DRY_RUN}"
          ALLOW_ENFORCE="${VCUDA_ALLOW_ENFORCE}"
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
          DRY_RUN="${VCUDA_DRY_RUN}"
          ALLOW_ENFORCE="${VCUDA_ALLOW_ENFORCE}"
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
