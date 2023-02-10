#include "common.h"
#include <string.h>
#include <time.h>
#include <limits.h>
#include <termios.h>
#include <errno.h>
#include <poll.h>

#ifdef DEBUG
#define printf(...) printf(__VA_ARGS__)
#else
#define printf(...)
#endif

uint8_t font[] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80, // F
};

uint8_t mem[4096] = {0};
uint8_t V[16] = {0};

uint16_t stack[16] = {0};
uint16_t sp = 0;
uint16_t I = 0;

uint16_t pc = 0x200;

uint8_t disp[32*64] = {0};

uint8_t df = 0;

uint8_t delay = 0;
uint8_t sound = 0;

struct pollfd pfd;

struct screen {
    int ttyfd;
    uint16_t w, h;
    struct termios raw, orig;
    int err;
} scr;

int screen_init(struct screen *scr) {
    int ret;
    ret = tcgetattr(STDIN_FILENO, &scr->orig);
    if (ret == -1) { 
        scr->err = errno;
        return -1;
    }
    scr->raw = scr->orig;
    scr->raw.c_lflag &= ~(ECHO | ICANON);
    ret = tcsetattr(STDIN_FILENO, TCSAFLUSH, &scr->raw);
    if (ret == -1) {
        scr->err = errno;
        return -1;
    }

    ret = open("/dev/tty", O_WRONLY);
    if (ret == -1) {
        scr->err = errno;
        return -1;
    }
    scr->ttyfd = ret;
    return 0;
}

void screen_deinit(struct screen scr) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &scr.orig);
    close(scr.ttyfd);
}

void load_rom(const char *path) {
	int fd;
	struct stat st;
	fd = open(path, O_RDONLY);
	if (fd == -1) errf("error opening file: '%s'", path);
	fstat(fd, &st);
	uint8_t *program = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);

	memcpy(mem+0x50, font, sizeof(font));
	memcpy(mem+0x200, program, st.st_size);
	
	munmap(program, st.st_size);
	close(fd);
}

void cycle(void) {
	uint16_t opcode = (uint16_t)mem[pc] << 8 | mem[pc + 1];
	uint8_t vn, x, y, height, pixel;

	switch (opcode & 0xF000) {
	case 0x0000:
		if (opcode & 0x00E0) {
			printf("clearing screen\n");
			memset(disp, 0, 32*64);
			pc += 2;
		} else if (opcode & 0x00EE) {
			printf("returning from subroutine\n");
			pc = stack[--sp];
		} else {
			printf("unimplemented (ml subroutine) %x\n", opcode);
		}
		break;
	case 0x1000:
		pc = opcode & 0x0FFF;
		printf("jumping to %x\n", pc);
		break;
	case 0x2000:
		printf("executing subroutine at %x\n", opcode & 0x0FFF);
		stack[sp++] = pc+2;
		pc = opcode & 0x0FFF;
		break;
	case 0x3000:
		vn = (opcode & 0x0F00) >> 8;
		printf("skipping if v%x == %x\n", vn, opcode & 0x00FF);
		if (V[vn] == (opcode & 0x00FF)) {
			pc += 2;
			printf("skipped\n");
		}
		pc += 2;
		break;
	case 0x4000:
		vn = (opcode & 0x0F00) >> 8;
		printf("skipping if v%x != %x\n", vn, opcode & 0x00FF);
		if (V[vn] != (opcode & 0x00FF)) {
			pc += 2;
			printf("skipped\n");
		}
		pc += 2;
		break;
	case 0x5000:
		x = (opcode & 0x0F00) >> 8;
		y = (opcode & 0x00F0) >> 4;
		printf("skipping if v%x == v%x\n", x, y);
		if (V[x] == V[y]) {
			pc += 2;
			printf("skipped\n");
		}
		pc += 2;
		break;
	case 0x6000:
		vn = (opcode & 0x0F00) >> 8;
		V[vn] = opcode & 0x00FF;
		printf("storing %x in v%x\n", V[vn], vn);
		pc += 2;
		break;
	case 0x7000:
		vn = (opcode & 0x0F00) >> 8;
		V[vn] += opcode & 0x00FF;
		printf("adding %x to v%x\n", opcode & 0x00FF, vn);
		pc += 2;
		break;
	case 0x8000:
		x = (opcode & 0x0F00) >> 8;
		y = (opcode & 0x00F0) >> 4;
		
		switch (opcode & 0x000F) {
		case 0x0:
			V[x] = V[y];
			break;
		case 0x1:
			V[x] |= V[y];
			break;
		case 0x2:
			V[x] &= V[y];
			break;
		case 0x3:
			V[x] ^= V[y];
			break;
		case 0x4:
			vn = V[x];
			if (V[x] + V[y] < vn || V[x] + V[y] > 255)
				V[0xF] = 1;
			V[x] += V[y];
			break;
		default:
			errf("unknown operation: %x\n", opcode);
		}
		pc += 1;
		break;
	case 0x9000:
		x = (opcode & 0x0F00) >> 8;
		y = (opcode & 0x00F0) >> 4;
		printf("skipping if v%x != v%x\n", x, y);
		if (V[x] != V[y]) {
			pc += 2;
			printf("skipped\n");
		}
		pc += 2;
		break;
	case 0xA000:
		I = opcode & 0x0FFF;
		printf("set I to %x\n", I);
		pc += 2;
		break;
	case 0xC000:
		vn = (opcode & 0x0F00) >> 8;
		V[vn] = (rand() % 255) & (opcode & 0x00FF);
		printf("set v%x to %x\n", vn, V[vn]);
		pc += 2;
		break;
	case 0xD000:
		x = V[(opcode & 0x0F00) >> 8];
		y = V[(opcode & 0x00F0) >> 4];
		height = opcode & 0x000F;

		printf("drawing sprite at (%x, %x)\n", x, y);

		V[0xF] = 0;

		for (uint8_t yl = 0; yl < height; yl++) {
			pixel = mem[I + yl];
			for (uint8_t xl = 0; xl < 8; xl++) {
				if ((pixel & (0x80 >> xl)) != 0) {
					uint8_t *d = disp+(x + xl + ((y + yl) * 64));
					if (*d == 1) {
						V[0xF] = 1;
					}
					*d ^= 1;
				}
			}
		}

		df = 1;
		pc += 2;
		break;
	case 0xE000:
		vn = (opcode & 0x0F00) >> 8;
		printf("skipping if key pressed is equal to value in v%x\n",
			   vn);
		if (poll(&pfd, 1, 100) > 0) {
			char c;
			read(0, &c, 1);
			if (c == V[vn]) {
				printf("skipping\n");
				pc += 2;
			}
		}
		pc += 2;
		break;
	case 0xF000:
		vn = (opcode & 0x0F00) >> 8;
		switch (opcode & 0x00FF) {
		case 0x07:
			V[vn] = delay;
			printf("setting v%x to delay %x\n", vn, delay);
			break;
		case 0x0A:
			while (poll(&pfd, 1, -1) == 0);
			char c;
			read(0, &c, 1);
			V[vn] = c;
			break;
		case 0x15:
			delay = V[vn];
			printf("setting delay to v%x (%x)\n", vn, delay);
			break;
		case 0x18:
			sound = V[vn];
			printf("setting sound to v%x (%x)\n", vn, sound);
			break;
		case 0x1E:
			I += V[vn];
			break;
		case 0x29:
			I = 0x50 + (V[vn] * 5);
			break;
		case 0x33:
			x = V[vn];
			mem[I] = x % 10;
			x /= 10;
			mem[I+1] = x % 10;
			x /= 10;
			mem[I+2] = x % 10;
			break;
		case 0x55:
			for (int i = 0; i < vn; i++)
				mem[I+i] = V[i];
			break;
		case 0x65:
			for (int i = 0; i < vn; i++)
				V[i] = I+i;
			break;
		default:
			errf("unknown operation: %x\n", opcode);
		}
		pc += 2;
		break;
	default:
		errf("unknown operation: %x\n", opcode);
	}
}

void print_regs(void) {
	int j = 0;
	for (int i = 0; i < 16; i++) {
		printf("v%x = %x\t", i, V[i]);
		if (++j % 4 == 0) printf("\n");
	}
}

void draw(void) {
	df = 0;
	for (int y = 0; y < 32; y++) {
		for (int x = 0; x < 64; x++) {
			if (disp[x + (y * 64)]) fprintf(stdout, "0");
			else fprintf(stdout, " ");
		}
		fprintf(stdout, "\n");
	}
}



int main(void) {
	srand(time(NULL));
	load_rom("test.ch8");

	if (screen_init(&scr) == -1) {
		errf("initializing screen: %s\n", strerror(scr.err));
	}

	pfd.fd = 0;
	pfd.events = POLLIN;

	struct timespec tmp, sp;
	clock_gettime(CLOCK_REALTIME, &sp);\
	
	for (;;) {
#ifndef DEBUG
		if (df) dprintf(1, "\x1b[2J\x1b[0;0H");
#endif
		clock_gettime(CLOCK_REALTIME, &tmp);
		if (tmp.tv_sec > sp.tv_sec && delay > 0) {
			delay -= (tmp.tv_sec - sp.tv_sec);
			sp = tmp;
		}
		cycle();
		printf("pc: %x\n", pc);
		print_regs();
		if (df) draw();
	}

	screen_deinit(scr);
}
