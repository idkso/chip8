#pragma once
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define err(str) errf("%s", str)
#define errf(str, ...)                                                         \
    do {                                                                       \
        fprintf(stderr, "error at %s:%d@%s: ", __FILE__, __LINE__, __func__);  \
        fprintf(stderr, str "\n", __VA_ARGS__);                                \
        exit(1);                                                               \
    } while (0);
