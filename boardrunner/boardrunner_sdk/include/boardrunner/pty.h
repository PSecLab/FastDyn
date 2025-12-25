#pragma once
#include <stdint.h>
#include <device.h>

int api_pty_fd_gen(void);
void api_pty_write_req(int fd, uint8_t value);
int api_pty_read_nonblock(int fd, uint8_t *buff);
