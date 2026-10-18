// SPDX-License-Identifier: MIT
// memdump: copy a physical memory range to stdout, read-only (angler).
// Used to read a crashed test boot's ramoops records (0x04000000, 1 MiB)
// from the known-good image, whose kernel has no ramoops node but reserves
// that range no-map, so /dev/mem can map it (read() refuses no-map memory).
//
//   memdump ADDR LEN > file      ADDR and LEN page-aligned, LEN <= 16 MiB
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: memdump ADDR LEN\n");
		return 2;
	}
	unsigned long addr = strtoul(argv[1], NULL, 0), len = strtoul(argv[2], NULL, 0);
	if ((addr | len) & 0xfff || !len || len > 16UL << 20) {
		fprintf(stderr, "memdump: ADDR and LEN must be page-aligned, LEN <= 16 MiB\n");
		return 2;
	}
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) {
		perror("memdump: /dev/mem");
		return 1;
	}
	volatile uint32_t *p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, addr);
	if (p == MAP_FAILED) {
		perror("memdump: mmap");
		return 1;
	}
	/* Device memory: copy with aligned word loads, never memcpy */
	static uint32_t buf[1024];
	for (unsigned long off = 0; off < len; off += sizeof(buf)) {
		for (unsigned i = 0; i < 1024; i++)
			buf[i] = p[off / 4 + i];
		if (fwrite(buf, sizeof(buf), 1, stdout) != 1)
			return 1;
	}
	return 0;
}
