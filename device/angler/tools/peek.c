// SPDX-License-Identifier: MIT
// peek: read-only 32-bit physical register reads for bring-up.
// Usage: peek ADDR [COUNT]  (prints COUNT words from ADDR, 4-byte aligned).
// It has no write path at all, unlike devmem, whose second argument writes.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: peek ADDR [COUNT]\n");
		return 2;
	}
	uint64_t addr = strtoull(argv[1], NULL, 0);
	unsigned long count = argc == 3 ? strtoul(argv[2], NULL, 0) : 1;
	long pg = sysconf(_SC_PAGESIZE);
	if (addr & 3 || count == 0 || count > 1024) {
		fprintf(stderr, "peek: ADDR must be 4-byte aligned, COUNT 1-1024\n");
		return 2;
	}
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) {
		perror("peek: /dev/mem");
		return 1;
	}
	uint64_t base = addr & ~(uint64_t)(pg - 1);
	size_t len = (addr - base) + count * 4;
	len = (len + pg - 1) & ~(size_t)(pg - 1);
	volatile uint32_t *map = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, base);
	if (map == MAP_FAILED) {
		perror("peek: mmap");
		return 1;
	}
	for (unsigned long i = 0; i < count; i++) {
		uint64_t a = addr + i * 4;
		printf("0x%08llx: 0x%08x\n", (unsigned long long)a, map[(a - base) / 4]);
	}
	return 0;
}
