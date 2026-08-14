#ifndef COV_BBL_H
#define COV_BBL_H

void fuzz_bbl_add(uint32_t pc, int irq_depth);
void fuzz_dump_bbl(void);
void fuzz_bbl_init(int argc, char **argv);
void fuzz_bbl_reset_trace(void);

#endif
