# SPDX-License-Identifier: GPL-2.0-only

CUDA_HOME ?= /usr/local/cuda
NVCC ?= $(CUDA_HOME)/bin/nvcc

TARGETS := cuda_malloc_smoke

.PHONY: all clean run

all: $(TARGETS)

cuda_malloc_smoke: cuda_malloc_smoke.cu
	$(NVCC) -O2 -std=c++14 -o $@ $<

run: cuda_malloc_smoke
	./cuda_malloc_smoke

clean:
	rm -f $(TARGETS)
