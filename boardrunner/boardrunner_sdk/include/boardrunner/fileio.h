#pragma once
#include <device.h>   // ConfigSection, etc.
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define _FILE_OFFSET_BITS 64

// --- File I/O APIs for Storage Models ---
int api_file_open(const char *filename, int mode);
int api_file_pread(int fd, void *buf, int len, uint64_t offset);
int api_file_pread_fill(int fd, void *buf, int len, uint64_t offset, uint8_t fill_val);
int api_file_pwrite(int fd, const void *buf, int len, uint64_t offset);
uint64_t api_file_get_size(int fd);
void api_file_close(int fd);
