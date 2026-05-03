#ifndef PROTOCOL_FUZZERS_H
#define PROTOCOL_FUZZERS_H

// lwip_http_fuzzer.c
void fuzz_plugin_lwip_http_fuzzer(char *buff, size_t len);
void fuzz_plugin_mqtt_fuzzer(char *buff, size_t len);

// mqtt.c
void fuzz_mqtt_in(unsigned int cpu_index, void *udata);

#endif