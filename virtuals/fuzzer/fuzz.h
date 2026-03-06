#ifndef FUZZER_H
#define FUZZER_H

uint32_t fuzz_get_register(int reg);
void fuzz_set_register(uint32_t value, int reg);

int fuzz_write_memory(unsigned long long addr, uint8_t *mem_buf, int len);
int fuzz_read_memory(unsigned long long addr, uint8_t *mem_buf, int len);

#endif