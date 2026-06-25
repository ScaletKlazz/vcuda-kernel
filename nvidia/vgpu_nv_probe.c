// SPDX-License-Identifier: GPL-2.0-only
#include <linux/moduleparam.h>
#include <linux/stddef.h>
#include <linux/string.h>

#include "vgpu_nv_570_symbols.h"
#include "vgpu_nv_probe.h"
#include "vgpu_trace.h"

static uint nvidia_driver_major;
static uint nvidia_driver_minor;
static uint nvidia_driver_patch;
static bool nvidia_okm_detected;
static bool nvidia_gsp_enabled;

module_param(nvidia_driver_major, uint, 0444);
MODULE_PARM_DESC(nvidia_driver_major, "NVIDIA driver major version override");
module_param(nvidia_driver_minor, uint, 0444);
MODULE_PARM_DESC(nvidia_driver_minor, "NVIDIA driver minor version override");
module_param(nvidia_driver_patch, uint, 0444);
MODULE_PARM_DESC(nvidia_driver_patch, "NVIDIA driver patch version override");
module_param(nvidia_okm_detected, bool, 0444);
MODULE_PARM_DESC(nvidia_okm_detected, "NVIDIA Open Kernel Modules detected");
module_param(nvidia_gsp_enabled, bool, 0444);
MODULE_PARM_DESC(nvidia_gsp_enabled, "NVIDIA GSP firmware enabled");

static struct vgpu_driver_fingerprint vgpu_nv_fingerprint;

static u64 vgpu_nv_hash_bytes(const void *data, size_t len)
{
	const unsigned char *bytes = data;
	u64 hash = 1469598103934665603ULL;
	size_t i;

	for (i = 0; i < len; i++) {
		hash ^= bytes[i];
		hash *= 1099511628211ULL;
	}

	return hash;
}

static void vgpu_nv_assign_capabilities(struct vgpu_driver_fingerprint *fp)
{
	fp->capabilities = 0;

	if (fp->driver_major == VGPU_NV_SUPPORTED_MAJOR)
		fp->capabilities |= VGPU_CAP_PROBE_ONLY;
}

static void vgpu_nv_finalize_hash(struct vgpu_driver_fingerprint *fp)
{
	u64 hash;

	fp->symbol_hash = 0;
	if (!fp->driver_major)
		return;

	hash = vgpu_nv_hash_bytes(fp, offsetof(struct vgpu_driver_fingerprint,
					       symbol_hash));
	fp->symbol_hash = hash;
}

int vgpu_nv_probe_init(void)
{
	memset(&vgpu_nv_fingerprint, 0, sizeof(vgpu_nv_fingerprint));

	vgpu_nv_fingerprint.driver_major = nvidia_driver_major;
	vgpu_nv_fingerprint.driver_minor = nvidia_driver_minor;
	vgpu_nv_fingerprint.driver_patch = nvidia_driver_patch;
	vgpu_nv_fingerprint.okm_detected = nvidia_okm_detected;
	vgpu_nv_fingerprint.gsp_enabled = nvidia_gsp_enabled;

	vgpu_nv_assign_capabilities(&vgpu_nv_fingerprint);
	vgpu_nv_finalize_hash(&vgpu_nv_fingerprint);

	vgpu_pr_info("nvidia probe driver=%u.%u.%u okm=%u gsp=%u caps=0x%x hash=%llu\n",
		     vgpu_nv_fingerprint.driver_major,
		     vgpu_nv_fingerprint.driver_minor,
		     vgpu_nv_fingerprint.driver_patch,
		     vgpu_nv_fingerprint.okm_detected,
		     vgpu_nv_fingerprint.gsp_enabled,
		     vgpu_nv_fingerprint.capabilities,
		     (unsigned long long)vgpu_nv_fingerprint.symbol_hash);

	return 0;
}

void vgpu_nv_probe_exit(void)
{
	memset(&vgpu_nv_fingerprint, 0, sizeof(vgpu_nv_fingerprint));
}

void vgpu_nv_probe_get_fingerprint(struct vgpu_driver_fingerprint *out)
{
	if (!out)
		return;

	*out = vgpu_nv_fingerprint;
}
