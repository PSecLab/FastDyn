#ifndef STATEFUL_FUZZERS_H
#define STATEFUL_FUZZERS_H

// lwip_ip.c
void fuzz_snap_handler(unsigned int cpu_index, void *udata);
void fuzz_eth_in(unsigned int cpu_index, void *udata);
void fuzz_eth_out(unsigned int cpu_index, void *udata);
void fuzz_pbuf_free(unsigned int cpu_index, void *udata);

void fuzz_plugin_lwip_ip_fuzzer_init(void);
void fuzz_plugin_lwip_ip_fuzzer(char *buff, size_t len);
void fuzz_plugin_lwip_ip_recv(uint8_t *buf, size_t len);
void fuzz_plugin_lwip_ip_fuzzer_exit(void);

#endif