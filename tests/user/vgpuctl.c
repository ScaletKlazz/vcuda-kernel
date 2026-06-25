// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "vgpu_ioctl.h"

static void usage(FILE *stream)
{
	fprintf(stream,
		"usage:\n"
		"  vgpuctl set --tgid N --gpu N --mem BYTES --weight N --memory on|off --compute on|off --dry-run on|off\n"
		"  vgpuctl get --tgid N --gpu N\n"
		"  vgpuctl stats\n"
		"  vgpuctl dry-run on|off\n"
		"  vgpuctl disable\n");
}

static int parse_i32(const char *text, int *out)
{
	char *end = NULL;
	long value;

	errno = 0;
	value = strtol(text, &end, 0);
	if (errno || !end || *end != '\0')
		return -1;
	if (value < INT32_MIN || value > INT32_MAX)
		return -1;
	*out = (int)value;
	return 0;
}

static int parse_u64(const char *text, uint64_t *out)
{
	char *end = NULL;
	unsigned long long value;

	errno = 0;
	value = strtoull(text, &end, 0);
	if (errno || !end || *end != '\0')
		return -1;
	*out = value;
	return 0;
}

static int parse_on_off(const char *text, int *out)
{
	if (!strcmp(text, "on")) {
		*out = 1;
		return 0;
	}
	if (!strcmp(text, "off")) {
		*out = 0;
		return 0;
	}
	return -1;
}

static int open_ctl(void)
{
	int fd = open("/dev/vgpuctl", O_RDWR | O_CLOEXEC);

	if (fd < 0)
		perror("open /dev/vgpuctl");
	return fd;
}

static void print_policy(const struct vgpu_policy *policy)
{
	printf("tgid=%d gpu_minor=%d memory_limit_bytes=%" PRIu64
	       " compute_weight=%u memory=%s compute=%s dry_run=%s\n",
	       policy->tgid, policy->gpu_minor,
	       (uint64_t)policy->memory_limit_bytes, policy->compute_weight,
	       (policy->flags & VGPU_POLICY_F_MEMORY) ? "on" : "off",
	       (policy->flags & VGPU_POLICY_F_COMPUTE) ? "on" : "off",
	       (policy->flags & VGPU_POLICY_F_DRY_RUN) ? "on" : "off");
}

static int command_set(int argc, char **argv)
{
	static const struct option options[] = {
		{ "tgid", required_argument, NULL, 't' },
		{ "gpu", required_argument, NULL, 'g' },
		{ "mem", required_argument, NULL, 'm' },
		{ "weight", required_argument, NULL, 'w' },
		{ "memory", required_argument, NULL, 'M' },
		{ "compute", required_argument, NULL, 'C' },
		{ "dry-run", required_argument, NULL, 'd' },
		{ NULL, 0, NULL, 0 },
	};
	struct vgpu_policy policy = {
		.tgid = -1,
		.gpu_minor = -1,
		.memory_limit_bytes = 0,
		.compute_weight = 100,
		.flags = 0,
	};
	uint64_t mem = 0;
	int enabled;
	int opt;
	int fd;
	int ret;

	optind = 2;
	while ((opt = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (opt) {
		case 't':
			if (parse_i32(optarg, &policy.tgid))
				return -EINVAL;
			break;
		case 'g':
			if (parse_i32(optarg, &policy.gpu_minor))
				return -EINVAL;
			break;
		case 'm':
			if (parse_u64(optarg, &mem))
				return -EINVAL;
			policy.memory_limit_bytes = mem;
			break;
		case 'w':
			if (parse_i32(optarg, &enabled))
				return -EINVAL;
			policy.compute_weight = enabled;
			break;
		case 'M':
			if (parse_on_off(optarg, &enabled))
				return -EINVAL;
			if (enabled)
				policy.flags |= VGPU_POLICY_F_MEMORY;
			break;
		case 'C':
			if (parse_on_off(optarg, &enabled))
				return -EINVAL;
			if (enabled)
				policy.flags |= VGPU_POLICY_F_COMPUTE;
			break;
		case 'd':
			if (parse_on_off(optarg, &enabled))
				return -EINVAL;
			if (enabled)
				policy.flags |= VGPU_POLICY_F_DRY_RUN;
			break;
		default:
			return -EINVAL;
		}
	}

	if (policy.tgid <= 0 || policy.gpu_minor < 0)
		return -EINVAL;

	fd = open_ctl();
	if (fd < 0)
		return -errno;
	ret = ioctl(fd, VGPU_IOCTL_SET_POLICY, &policy);
	if (ret < 0)
		ret = -errno;
	close(fd);
	return ret;
}

static int command_get(int argc, char **argv)
{
	static const struct option options[] = {
		{ "tgid", required_argument, NULL, 't' },
		{ "gpu", required_argument, NULL, 'g' },
		{ NULL, 0, NULL, 0 },
	};
	struct vgpu_policy_query query = { .tgid = -1, .gpu_minor = -1 };
	int opt;
	int fd;
	int ret;

	optind = 2;
	while ((opt = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (opt) {
		case 't':
			if (parse_i32(optarg, &query.tgid))
				return -EINVAL;
			break;
		case 'g':
			if (parse_i32(optarg, &query.gpu_minor))
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
	}

	if (query.tgid <= 0 || query.gpu_minor < 0)
		return -EINVAL;

	fd = open_ctl();
	if (fd < 0)
		return -errno;
	ret = ioctl(fd, VGPU_IOCTL_GET_POLICY, &query);
	if (ret < 0) {
		ret = -errno;
	} else if (query.found) {
		print_policy(&query.policy);
	} else {
		printf("not_found tgid=%d gpu_minor=%d\n", query.tgid,
		       query.gpu_minor);
	}
	close(fd);
	return ret;
}

static int command_stats(void)
{
	struct vgpu_stats stats;
	int fd;
	int ret;

	fd = open_ctl();
	if (fd < 0)
		return -errno;
	ret = ioctl(fd, VGPU_IOCTL_GET_STATS, &stats);
	if (ret < 0) {
		ret = -errno;
	} else {
		printf("runtime_mode=%u policies_set=%" PRIu64
		       " ioctl_seen=%" PRIu64
		       " memory_alloc_seen=%" PRIu64
		       " memory_free_seen=%" PRIu64
		       " memory_would_deny=%" PRIu64
		       " memory_denied=%" PRIu64
		       " timeslice_seen=%" PRIu64
		       " timeslice_would_rewrite=%" PRIu64
		       " timeslice_rewritten=%" PRIu64
		       " hook_errors=%" PRIu64
		       " emergency_disabled=%" PRIu64
		       " events_dropped=%" PRIu64
		       " unmatched_free=%" PRIu64 "\n",
		       stats.runtime_mode, (uint64_t)stats.policies_set,
		       (uint64_t)stats.ioctl_seen,
		       (uint64_t)stats.memory_alloc_seen,
		       (uint64_t)stats.memory_free_seen,
		       (uint64_t)stats.memory_would_deny,
		       (uint64_t)stats.memory_denied,
		       (uint64_t)stats.timeslice_seen,
		       (uint64_t)stats.timeslice_would_rewrite,
		       (uint64_t)stats.timeslice_rewritten,
		       (uint64_t)stats.hook_errors,
		       (uint64_t)stats.emergency_disabled,
		       (uint64_t)stats.events_dropped,
		       (uint64_t)stats.unmatched_free);
	}
	close(fd);
	return ret;
}

static int command_dry_run(const char *value)
{
	int enabled;
	int fd;
	int ret;

	if (parse_on_off(value, &enabled))
		return -EINVAL;
	fd = open_ctl();
	if (fd < 0)
		return -errno;
	ret = ioctl(fd, VGPU_IOCTL_SET_DRY_RUN, &enabled);
	if (ret < 0)
		ret = -errno;
	close(fd);
	return ret;
}

static int command_disable(void)
{
	int fd;
	int ret;

	fd = open_ctl();
	if (fd < 0)
		return -errno;
	ret = ioctl(fd, VGPU_IOCTL_DISABLE);
	if (ret < 0)
		ret = -errno;
	close(fd);
	return ret;
}

int main(int argc, char **argv)
{
	int ret;

	if (argc < 2) {
		usage(stderr);
		return 2;
	}

	if (!strcmp(argv[1], "set"))
		ret = command_set(argc, argv);
	else if (!strcmp(argv[1], "get"))
		ret = command_get(argc, argv);
	else if (!strcmp(argv[1], "stats"))
		ret = command_stats();
	else if (!strcmp(argv[1], "dry-run") && argc == 3)
		ret = command_dry_run(argv[2]);
	else if (!strcmp(argv[1], "disable"))
		ret = command_disable();
	else
		ret = -EINVAL;

	if (ret) {
		usage(stderr);
		fprintf(stderr, "error=%d (%s)\n", ret, strerror(-ret));
		return 1;
	}

	return 0;
}
