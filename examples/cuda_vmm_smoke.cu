// SPDX-License-Identifier: GPL-2.0-only
#include <cuda.h>

#include <cstdlib>
#include <iostream>

static int fail(CUresult result, const char *op)
{
	if (result == CUDA_SUCCESS)
		return 0;

	const char *name = "unknown";
	const char *text = "unknown";
	cuGetErrorName(result, &name);
	cuGetErrorString(result, &text);
	std::cerr << op << " failed: " << name << ": " << text << "\n";
	return 1;
}

int main(int argc, char **argv)
{
	size_t size = 256ULL * 1024ULL * 1024ULL;
	int loops = 4;
	int device_id = 0;

	if (argc > 1)
		size = std::strtoull(argv[1], nullptr, 0);
	if (argc > 2)
		loops = std::atoi(argv[2]);
	if (argc > 3)
		device_id = std::atoi(argv[3]);
	if (size == 0 || loops <= 0)
		return 2;

	if (fail(cuInit(0), "cuInit"))
		return 1;

	CUdevice device;
	if (fail(cuDeviceGet(&device, device_id), "cuDeviceGet"))
		return 1;

	CUcontext context;
	if (fail(cuDevicePrimaryCtxRetain(&context, device), "cuDevicePrimaryCtxRetain"))
		return 1;
	if (fail(cuCtxSetCurrent(context), "cuCtxSetCurrent"))
		return 1;

	CUmemAllocationProp prop = {};
	prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
	prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
	prop.location.id = device_id;

	size_t granularity = 0;
	if (fail(cuMemGetAllocationGranularity(&granularity, &prop,
						 CU_MEM_ALLOC_GRANULARITY_MINIMUM),
		 "cuMemGetAllocationGranularity"))
		return 1;

	size_t aligned_size = ((size + granularity - 1) / granularity) * granularity;

	for (int i = 0; i < loops; i++) {
		CUmemGenericAllocationHandle handle;
		CUdeviceptr ptr = 0;

		if (fail(cuMemAddressReserve(&ptr, aligned_size, 0, 0, 0),
			 "cuMemAddressReserve"))
			return 1;
		if (fail(cuMemCreate(&handle, aligned_size, &prop, 0), "cuMemCreate"))
			return 1;
		if (fail(cuMemMap(ptr, aligned_size, 0, handle, 0), "cuMemMap"))
			return 1;

		CUmemAccessDesc access = {};
		access.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
		access.location.id = device_id;
		access.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
		if (fail(cuMemSetAccess(ptr, aligned_size, &access, 1),
			 "cuMemSetAccess"))
			return 1;

		if (fail(cuMemUnmap(ptr, aligned_size), "cuMemUnmap"))
			return 1;
		if (fail(cuMemRelease(handle), "cuMemRelease"))
			return 1;
		if (fail(cuMemAddressFree(ptr, aligned_size), "cuMemAddressFree"))
			return 1;
	}

	if (fail(cuDevicePrimaryCtxRelease(device), "cuDevicePrimaryCtxRelease"))
		return 1;

	std::cout << "cuda_vmm_smoke ok size=" << aligned_size
		  << " requested=" << size
		  << " granularity=" << granularity
		  << " loops=" << loops
		  << " device=" << device_id << "\n";
	return 0;
}