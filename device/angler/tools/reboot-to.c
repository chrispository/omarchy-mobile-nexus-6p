// SPDX-License-Identifier: MIT
// reboot-to: restart the Nexus 6P (angler) into fastboot, recovery or a
// normal boot without buttons.
//
// The angler bootloader reads a restart reason from shared IMEM after a warm
// reset (the PS_HOLD restart Linux does) and clears it. Android's
// "reboot bootloader" works the same way.
//
//   reboot-to bootloader | recovery | normal
//
// The only thing it writes is the restart-reason word. Angler-specific.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <unistd.h>

#define IMEM_PAGE		0xfe87f000UL
#define RESTART_REASON		0x65c

static const struct { const char *name; uint32_t magic; } reasons[] = {
	{ "bootloader", 0x77665500 },
	{ "normal", 0x77665501 },
	{ "recovery", 0x77665502 },
};

int main(int argc, char **argv)
{
	uint32_t magic = 0;

	for (size_t i = 0; argc == 2 && i < sizeof(reasons) / sizeof(reasons[0]); i++)
		if (!strcmp(argv[1], reasons[i].name))
			magic = reasons[i].magic;
	if (!magic) {
		fprintf(stderr, "usage: reboot-to bootloader | recovery | normal\n");
		return 2;
	}
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("reboot-to: /dev/mem");
		return 1;
	}
	volatile uint32_t *imem = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, IMEM_PAGE);
	if (imem == MAP_FAILED) {
		perror("reboot-to: map IMEM");
		return 1;
	}
	imem[RESTART_REASON / 4] = magic;
	if (imem[RESTART_REASON / 4] != magic) {
		fprintf(stderr, "reboot-to: restart reason did not stick\n");
		return 1;
	}
	sync();
	reboot(RB_AUTOBOOT);
	perror("reboot-to: reboot");
	return 1;
}
