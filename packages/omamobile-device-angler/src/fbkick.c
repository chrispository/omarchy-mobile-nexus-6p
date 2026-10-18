// SPDX-License-Identifier: MIT
// fbkick: show the bootloader's splash framebuffer on the Nexus 6P (angler)
// without a display driver (nexus_linux_plan.md N2).
//
// The bootloader leaves MDSS 1.9 set up for its logo: a 1440x2560 RGB888
// buffer at 0x03400000 (stride 4320), scanned out as two 720-wide halves by
// VIG0/VIG1 through CTL0/CTL1 to the two DSI links of a command-mode panel.
// A command-mode panel only takes a new frame when the CTL is started, so
// writes to the buffer stay invisible until a kick.
//
//   fbkick kick          start CTL0 and CTL1 once (one frame)
//   fbkick loop [HZ]     kick HZ times a second (default 10), forever
//   fbkick pattern       draw colour bars over rows 600-1000, then kick
//
// The only things it can write are pixels inside the splash buffer and the
// START registers of CTL0 and CTL1. Angler-specific: never run it elsewhere.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MDSS_BASE	0xfd900000UL
#define MDSS_HW_VERSION	0x10090000U	/* MDSS 1.9, msm8x94 */
#define CTL_START(n)	(0x2000 + (n) * 0x200 + 0x1c)	/* MDP at MDSS + 0x1000 */
#define VIG0_SRC0_ADDR	(0x5000 + 0x14)
#define MDSS_MAP_LEN	0x8000	/* through VIG1 (0x7000) */

#define FB_BASE		0x03400000UL
#define FB_W		1440
#define FB_H		2560
#define FB_STRIDE	(FB_W * 3)

static volatile uint32_t *mdss;

static void kick(void)
{
	mdss[CTL_START(0) / 4] = 1;
	mdss[CTL_START(1) / 4] = 1;
}

static int pattern(int fd)
{
	size_t len = (size_t)FB_STRIDE * FB_H;
	uint8_t *fb = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, FB_BASE);
	static const uint8_t bars[8][3] = {	/* bytes in memory order: B, G, R */
		{ 255, 255, 255 }, { 0, 255, 255 }, { 255, 255, 0 }, { 0, 255, 0 },
		{ 255, 0, 255 }, { 0, 0, 255 }, { 255, 0, 0 }, { 0, 0, 0 },
	};

	if (fb == MAP_FAILED) {
		perror("fbkick: map framebuffer");
		return 1;
	}
	/*
	 * /dev/mem maps the buffer as device memory, where an unaligned access
	 * faults: build a row here and store it as aligned 32-bit words.
	 */
	static uint32_t row[FB_STRIDE / 4];
	for (int x = 0; x < FB_W; x++)
		memcpy((uint8_t *)row + x * 3, bars[x * 8 / FB_W], 3);
	for (int y = 600; y < 1000; y++) {
		volatile uint32_t *dst = (volatile uint32_t *)(fb + (size_t)y * FB_STRIDE);
		for (size_t i = 0; i < FB_STRIDE / 4; i++)
			dst[i] = row[i];
	}
	munmap(fb, len);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: fbkick kick | loop [HZ] | pattern\n");
		return 2;
	}
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("fbkick: /dev/mem");
		return 1;
	}
	mdss = mmap(NULL, MDSS_MAP_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, MDSS_BASE);
	if (mdss == MAP_FAILED) {
		perror("fbkick: map MDSS");
		return 1;
	}
	/* Only touch an MDSS that the bootloader left as described above */
	if (mdss[0] != MDSS_HW_VERSION || mdss[VIG0_SRC0_ADDR / 4] != FB_BASE) {
		fprintf(stderr, "fbkick: MDSS 0x%08x, VIG0 buffer 0x%08x: not the angler splash setup\n",
			mdss[0], mdss[VIG0_SRC0_ADDR / 4]);
		return 1;
	}
	if (!strcmp(argv[1], "kick")) {
		kick();
	} else if (!strcmp(argv[1], "pattern")) {
		if (pattern(fd))
			return 1;
		kick();
	} else if (!strcmp(argv[1], "loop")) {
		int hz = argc > 2 ? atoi(argv[2]) : 10;
		if (hz < 1 || hz > 60)
			hz = 10;
		for (;;) {
			kick();
			usleep(1000000 / hz);
		}
	} else {
		fprintf(stderr, "fbkick: unknown command %s\n", argv[1]);
		return 2;
	}
	return 0;
}
