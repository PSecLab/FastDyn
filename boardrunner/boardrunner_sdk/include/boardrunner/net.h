#pragma once
#include <stdint.h>
#include <device.h>

int api_tap_init(const char *dev_name);
int api_tap_send(int fd, const uint8_t *buf, int len);
int api_tap_recv_nonblock(int fd, uint8_t *buf, int max_len);
