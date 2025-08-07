#ifndef VIRTUAL_H
#define VIRTUAL_H

#include <qemu-plugin.h>

#define ARM_V7M_R0    0
#define ARM_V7M_R1    1
#define ARM_V7M_R2    2
#define ARM_V7M_R3    3
#define ARM_V7M_R4    4
#define ARM_V7M_R5    5
#define ARM_V7M_R6    6
#define ARM_V7M_R7    7
#define ARM_V7M_R8    8
#define ARM_V7M_R9    9
#define ARM_V7M_R10  10
#define ARM_V7M_R11  11
#define ARM_V7M_R12  12
#define ARM_V7M_SP   13  // Stack Pointer
#define ARM_V7M_LR   14  // Link Register
#define ARM_V7M_PC   15  // Program Counter

#define ARM_V7M_S0   26
#define ARM_V7M_S1   27
#define ARM_V7M_S2   28
#define ARM_V7M_S3   29
#define ARM_V7M_S4   30
#define ARM_V7M_S5   31
#define ARM_V7M_S6   32
#define ARM_V7M_S7   33
#define ARM_V7M_S8   34
#define ARM_V7M_S9   35
#define ARM_V7M_S10  36
#define ARM_V7M_S11  37
#define ARM_V7M_S12  38
#define ARM_V7M_S13  39
#define ARM_V7M_S14  40
#define ARM_V7M_S15  41
#define ARM_V7M_S16  42
#define ARM_V7M_S17  43
#define ARM_V7M_S18  44
#define ARM_V7M_S19  45
#define ARM_V7M_S20  46
#define ARM_V7M_S21  47
#define ARM_V7M_S22  48
#define ARM_V7M_S23  49
#define ARM_V7M_S24  50
#define ARM_V7M_S25  51
#define ARM_V7M_S26  52
#define ARM_V7M_S27  53
#define ARM_V7M_S28  54
#define ARM_V7M_S29  55
#define ARM_V7M_S30  56
#define ARM_V7M_S31  57

#define ARM_V7M_D0  26
#define ARM_V7M_D1  27
#define ARM_V7M_D2  28
#define ARM_V7M_D3  29
#define ARM_V7M_D4  30
#define ARM_V7M_D5  31
#define ARM_V7M_D6  32
#define ARM_V7M_D7  33
#define ARM_V7M_D8  34
#define ARM_V7M_D9  35
#define ARM_V7M_D10 36
#define ARM_V7M_D11 37
#define ARM_V7M_D12 38
#define ARM_V7M_D13 39
#define ARM_V7M_D14 40
#define ARM_V7M_D15 41


typedef void (*cb_func_t)(unsigned int cpu_index, void *userdata);

typedef struct {
    const char *name;
    cb_func_t func;
} cb_entry_t;

typedef struct {
    unsigned long long address;
    cb_func_t func;        // function pointer, NOT the name
    char args[384];
} rule_t;


typedef union {
		float  f;
        uint32_t i;
} FloatConverter;



typedef union {
        double d;
		float f[2];
        uint32_t i[2];
} DoubleConverter;






bool find_rule_by_address(unsigned long long addr, rule_t **out_rule);




#endif // VIRTUAL_H
