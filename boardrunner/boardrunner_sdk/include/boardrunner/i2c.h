#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <device.h>   // ConfigSection, etc.

enum i2c_event {
    I2C_START_RECV,
    I2C_START_SEND,
    I2C_FINISH,
    I2C_NACK
};

typedef int     (*SlaveSendFunc)(uint8_t data);
typedef uint8_t (*SlaveRecvFunc)(void);
typedef int     (*SlaveEventFunc)(enum i2c_event event);

typedef struct {
    char* name;
    uint16_t address;      // you already store as uint16_t
    SlaveSendFunc send;
    SlaveRecvFunc recv;
    SlaveEventFunc event;
} SlaveDetails;

typedef struct {
    int num_slaves;
    SlaveDetails* slave;
} SlaveList;

typedef struct {
    SlaveList Slaves;
    SlaveDetails* current_dev;
    uint8_t saved_address;
} I2CBus;

// Public APIs
I2CBus  api_i2c_init_bus(ConfigSection* model_info);
int     api_i2c_start_transfer(I2CBus* bus, uint8_t address, bool is_recv);
int     api_i2c_start_transfer_10bit(I2CBus* bus, uint16_t address, bool is_recv);
void    api_i2c_end_transfer(I2CBus* bus);
int     api_i2c_send(I2CBus* bus, uint8_t data);
uint8_t api_i2c_recv(I2CBus* bus);
