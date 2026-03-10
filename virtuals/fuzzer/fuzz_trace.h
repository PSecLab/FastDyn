#ifndef FUZZ_TRACE_H
#define FUZZ_TRACE_H

void fuzz_trace_add_value(uint32_t val);
void fuzz_trace_finish_run(void);
int fuzz_trace_init();

#endif