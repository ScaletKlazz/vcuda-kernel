// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../include/vgpu_ioctl.h"

static unsigned long long parse_u64(const char *value)
{
	char *end = NULL;
	unsigned long long parsed;

	errno = 0;
	parsed = strtoull(value, &end, 0);
	if (errno || !end || *end != '\0') {
		fprintf(stderr, "invalid integer: %s\n", value);
		exit(2);
	}

	return parsed;
}

int main(int argc, char **argv)
{
	struct vgpu_policy policy = { 0 };
	int fd;
	int ret;

	if (argc < 4 || argc > 6) {
		fprintf(stderr,
			"usage: %s <tgid> <gpu_minor> <compute_weight> [flags] [memory_limit_bytes]\n",
			argv[0]);
		fprintf(stderr,
			"default flags: VGPU_POLICY_F_COMPUTE (0x2); dry-run flag: 0x4\n");
		return 2;
	}

	policy.tgid = (int32_t)parse_u64(argv[1]);
	policy.gpu_minor = (int32_t)parse_u64(argv[2]);
	policy.compute_weight = (uint32_t)parse_u64(argv[3]);
	policy.flags = argc > 4 ? (uint32_t)parse_u64(argv[4]) : VGPU_POLICY_F_COMPUTE;
	policy.memory_limit_bytes = argc > 5 ? parse_u64(argv[5]) : 1;

	fd = open("/dev/vgpuctl", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open /dev/vgpuctl");
		return 1;
	}

	ret = ioctl(fd, VGPU_IOCTL_SET_POLICY, &policy);
	if (ret < 0) {
		perror("ioctl VGPU_IOCTL_SET_POLICY");
		close(fd);
		return 1;
	}

	printf("policy set tgid=%d gpu_minor=%d compute_weight=%u flags=0x%08x memory_limit_bytes=%llu\n",
	       policy.tgid, policy.gpu_minor, policy.compute_weight, policy.flags,
	       (unsigned long long)policy.memory_limit_bytes);
	close(fd);
	return 0;
}
