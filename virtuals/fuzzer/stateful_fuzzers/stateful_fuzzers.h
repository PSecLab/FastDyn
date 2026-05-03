#ifndef STATEFUL_FUZZERS_H
#define STATEFUL_FUZZERS_H

// lwip_ip.c
void fuzz_snap_handler(unsigned int cpu_index, void *udata);
void fuzz_eth_in(unsigned int cpu_index, void *udata);
void fuzz_eth_out(unsigned int cpu_index, void *udata);
void fuzz_pbuf_free(unsigned int cpu_index, void *udata);

void fuzz_plugin_lwip_ip_fuzzer_exit(void);

// modbus.c
void fuzz_modbus_enable();
void fuzz_modbus_MB_USART_Poll(unsigned int cpu_index, void *udata);
void fuzz_xMBPortSerialGetByte(unsigned int cpu_index, void *udata);
void fuzz_xMBPortSerialPutByte(unsigned int cpu_index, void *udata);

#endif