#pragma once
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>

/**
 * @brief Creates a Named Pipe (FIFO) at the specified path and opens it.
 * * Includes the O_RDWR trick so the pipe doesn't close when the external
 * writer disconnects.
 * * @param path The filesystem path (e.g., "/tmp/gpio_btn" or "/tmp/adc_in")
 * @return int File Descriptor (fd) on success, or -1 on failure.
 */
int api_fifo_open(const char *path);

/**
 * @brief Writes a buffer to the FIFO.
 * * @param fd The file descriptor returned by api_fifo_open.
 * @param data Pointer to data to write.
 * @param len Number of bytes to write.
 * @return int Number of bytes written, or -1 on error.
 */
int api_fifo_write(int fd, const void *data, int len);

/**
 * @brief Reads a single byte from the FIFO (Non-blocking).
 * * @param fd The file descriptor.
 * @param out_byte Pointer where the read byte will be stored.
 * @return 1 on success (byte read), 0 if empty, -1 on error.
 */
int api_fifo_read_nonblock(int fd, uint8_t *out_byte);

/**
 * @brief Closes the fd and deletes the file from the filesystem.
 * * @param fd The file descriptor.
 * @param path The path to unlink (remove).
 */
void api_fifo_close(int fd, const char *path);
