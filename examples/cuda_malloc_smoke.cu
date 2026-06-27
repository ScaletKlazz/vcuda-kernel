// SPDX-License-Identifier: GPL-2.0-only
#include <cuda_runtime.h>

#include <cstdlib>
#include <iostream>
#include <unistd.h>

static int fail(cudaError_t err, const char *op)
{
	if (err == cudaSuccess)
		return 0;

	std::cerr << op << " failed: " << cudaGetErrorString(err) << "\n";
	return 1;
}

int main(int argc, char **argv)
{
	size_t size = 256ULL * 1024ULL * 1024ULL;
	int loops = 4;
	int device = 0;
	int pre_sleep_seconds = 0;

	if (argc > 1)
		size = std::strtoull(argv[1], nullptr, 0);
	if (argc > 2)
		loops = std::atoi(argv[2]);
	if (argc > 3)
		device = std::atoi(argv[3]);
	if (argc > 4)
		pre_sleep_seconds = std::atoi(argv[4]);
	if (size == 0 || loops <= 0)
		return 2;
	if (pre_sleep_seconds > 0) {
		std::cout << "cuda_malloc_smoke pid=" << getpid()
			  << " sleeping=" << pre_sleep_seconds << "\n";
		std::cout.flush();
		sleep(pre_sleep_seconds);
	}

	if (fail(cudaSetDevice(device), "cudaSetDevice"))
		return 1;

	for (int i = 0; i < loops; i++) {
		void *ptr = nullptr;

		if (fail(cudaMalloc(&ptr, size), "cudaMalloc"))
			return 1;
		if (fail(cudaMemset(ptr, 0xa5, size), "cudaMemset"))
			return 1;
		if (fail(cudaDeviceSynchronize(), "cudaDeviceSynchronize"))
			return 1;
		if (fail(cudaFree(ptr), "cudaFree"))
			return 1;
	}

	std::cout << "cuda_malloc_smoke ok size=" << size
		  << " loops=" << loops
		  << " device=" << device << "\n";
	return 0;
}
