__attribute__((naked)) void init_regs(void) {
    __asm volatile (
        "mov r0,  #1\n\t"
        "mov r1,  #2\n\t"
        "mov r2,  #3\n\t"
        "mov r3,  #4\n\t"
        "mov r4,  #5\n\t"
        "mov r5,  #6\n\t"
        "mov r6,  #7\n\t"
        "mov r7,  #8\n\t"
        "mov r8,  #9\n\t"
        "mov r9,  #10\n\t"
        "mov r10, #11\n\t"
        "mov r11, #12\n\t"
        "mov r12, #13\n\t"
        "mov lr,  #14\n\t"

        // Infinite loop
		"bkpt #0\n\t"
        "b .\n\t"
    );
}
int main() {
#ifdef TRAIN
		timer_test();
#endif
		while(1);
}

