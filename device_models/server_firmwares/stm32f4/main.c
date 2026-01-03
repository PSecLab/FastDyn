//UNDER DEVELOPMENT NOTE: DONT REMOVE/CHANGE THE MAIN FILE! IF DO PLEASE KEEP THE CODE SEPARATE

// #include <stdint.h>

// /*
//  * These definitions are based on the STM32F76xxx Reference Manual (RM0123).
//  * In a real "bare-metal" project, these would be in your device header
//  * file (e.g., "stm32f767xx.h").
//  */

// /*
//  * @brief SDMMC Status register (SDMMC_STA)
//  * Offset: 0x34
//  * (Based on RM0123, Page 52)
//  */
// #define SDMMC_STA_CCRCFAIL  (1U << 0)  // Bit 0: Command response received (CRC check failed)
// #define SDMMC_STA_DCRCFAIL  (1U << 1)  // Bit 1: Data block sent/received (CRC check failed)
// #define SDMMC_STA_CTIMEOUT  (1U << 2)  // Bit 2: Command response timeout
// #define SDMMC_STA_DTIMEOUT  (1U << 3)  // Bit 3: Data timeout
// #define SDMMC_STA_TXUNDERR  (1U << 4)  // Bit 4: Transmit FIFO underrun error
// #define SDMMC_STA_DATAEND   (1U << 8)  // Bit 8: Data end (data counter is zero)
// #define SDMMC_STA_TXFIFOHE  (1U << 14) // Bit 14: Transmit FIFO half empty

// /*
//  * @brief SDMMC FIFO register (SDMMC_FIFO)
//  * Offset: 0x80
//  * (Based on RM0123, Page 58)
//  */
// // The FIFO register is at offset 0x80.

// /*
//  * @brief Main SDMMC Peripheral Register Structure
//  * This is a simplified version of the one found in the CMSIS device header.
//  * (e.g., SDMMC1_BASE would be defined as (SDMMC_TypeDef *) 0x40012C00)
//  */
// typedef struct
// {
//   volatile uint32_t POWER;      // Offset: 0x00
//   volatile uint32_t CLKCR;      // Offset: 0x04
//   volatile uint32_t ARG;        // Offset: 0x08
//   volatile uint32_t CMD;        // Offset: 0x0C
//   volatile const uint32_t RESPCMD;  // Offset: 0x10
//   volatile const uint32_t RESP1;    // Offset: 0x14
//   volatile const uint32_t RESP2;    // Offset: 0x18
//   volatile const uint32_t RESP3;    // Offset: 0x1C
//   volatile const uint32_t RESP4;    // Offset: 0x20
//   volatile uint32_t DTIMER;     // Offset: 0x24
//   volatile uint32_t DLEN;       // Offset: 0x28
//   volatile uint32_t DCTRL;      // Offset: 0x2C
//   volatile const uint32_t DCOUNT;   // Offset: 0x30
//   volatile const uint32_t STA;      // Offset: 0x34
//   volatile uint32_t ICR;        // Offset: 0x38
//   volatile uint32_t MASK;       // Offset: 0x3C
//   uint32_t      RESERVED0[2]; // Offsets 0x40, 0x44
//   volatile const uint32_t FIFOCNT;  // Offset: 0x48
//   uint32_t      RESERVED1[13]; // Offsets 0x4C - 0x7C
//   volatile uint32_t FIFO;       // Offset: 0x80
// } SDMMC_TypeDef;

// /*
//  * Define the base address for the SDMMC1 peripheral.
//  * This value is from the RM0410 reference manual for STM32F76xxx devices.
//  * In a real project, this would be in your stm32f767xx.h file.
//  */
// #define SDMMC1_BASE           (0x40012C00UL)
// #define SDMMC1                ((SDMMC_TypeDef *)SDMMC1_BASE)


// /*
//  * This function demonstrates the bare-metal translation of your loop.
//  * It now accepts a 'volatile uint32_t *' for direct word access.
//  *
//  * @param SDMMC1 Pointer to the SDMMC peripheral (e.g., SDMMC1)
//  * @param dataremaining_bytes The total number of bytes to write (e.g., 512)
//  * @param wordbuff Pointer to the source data buffer (must be 32-bit aligned)
//  *
//  * @retval 0 on success
//  * @retval -1 on failure
//  */
// int BareMetal_SD_WriteLoop(uint32_t dataremaining_bytes, volatile uint32_t *wordbuff)
// {
//   uint32_t count = 0;

//   // --- START OF BARE-METAL TRANSLATION ---

//   // Define the mask for the loop exit conditions
//   const uint32_t EXIT_FLAGS = SDMMC_STA_TXUNDERR | SDMMC_STA_DCRCFAIL | SDMMC_STA_DTIMEOUT | SDMMC_STA_DATAEND;
//   const uint32_t FAILURE_FLAGS = SDMMC_STA_TXUNDERR | SDMMC_STA_DCRCFAIL | SDMMC_STA_DTIMEOUT;

//   // Loop while no exit flags are set
//   while ( (SDMMC1->STA & EXIT_FLAGS) == 0 )
//   {
//     // Check if the FIFO is at least half empty (ready for 8 words)
//     // AND if we still have data left to write.
//     if ( (SDMMC1->STA & SDMMC_STA_TXFIFOHE) && (dataremaining_bytes > 0U) )
//     {
//       /* Write data to SDMMC Tx FIFO */
//       // We will write up to 8 words (32 bytes)
//       // This loop is now safe and will stop if dataremaining_bytes runs out
//       for (count = 0U; (count < 8U) && (dataremaining_bytes > 0U); count++)
//       {
//         // Directly write one 32-bit word from RAM to the 32-bit FIFO
//         SDMMC1->FIFO = *wordbuff;

//         // Advance the RAM buffer pointer by one word
//         wordbuff++;

//         // Decrement the remaining byte count by 4
//         dataremaining_bytes -= 4;
//       }
//     }
//   }

//   // --- END OF BARE-METAL TRANSLATION ---

//   // Check the status register to see why the loop terminated
//   uint32_t final_status = SDMMC1->STA;

//   // Check for failure flags first.
//   if (final_status & FAILURE_FLAGS)
//   {
//     return final_status; // Failure
//   }

//   // Check for success flag.
//   if (final_status & SDMMC_STA_DATAEND)
//   {
//     return 0; // Success
//   }

//   // Should not be reachable, but acts as a safety net.
//   return final_status;
// }

// void myfunc(void){
//     while(1);
// }

// void mynextfunc(int return_val){
//     while(1);
// }

// static inline void nop(void) {
//     __asm__ volatile ("nop" ::: "memory");
// }

// int main() {
//         /* Source buffer in RAM. Must be 512 bytes (128 words) we have to get the data from here*/
//         volatile uint32_t *ram_address  = (volatile uint32_t *)0x20000000;

//         /* Signal address for passthrough tool */
//         volatile uint32_t *signal_addr = (volatile uint32_t *)(0x20000800);

//         // //value of arg register
//         // volatile uint32_t *ram_address  = (volatile uint32_t *)0x20000804;

//         *signal_addr = 0;

//         //wait till the cpu changes the pc to start the required behavior
//         while (1) {
//             // int return_val;
//             // for (int i = 0; i< 32000; i++) {
//             // myfunc(); //wait inside till the passthrough changes pc -- dont worry about this, it will be changed

//             // // 1. Run the complete SDIO write logic.
//             // //    Pass the peripheral, byte count, and the word-aligned buffer.
//             // //    No cast is needed now.
//             // for (volatile int j=0; j<1000000; j++);    //some wait
//             // nop();
//             // nop();
//             // return_val = BareMetal_SD_WriteLoop(512, ram_address);

//             // // for (volatile int t=0; t<100; t++);
//             // // 2. Signal the passthrough to continue the execution
//             // if (return_val == 0){//signal success
//             //     *signal_addr = 24223u;
//             // } else {
//             //     *signal_addr = 105u;  //signal failure
//             // }
//             // }
//             // myfunc(); //wait inside till the passthrough changes pc -- dont worry about this, it will be changed

//             // // 1. Run the complete SDIO write logic.
//             // //    Pass the peripheral, byte count, and the word-aligned buffer.
//             // //    No cast is needed now.
//             // nop();
//             // nop();
//             // int return_val = BareMetal_SD_WriteLoop(512, ram_address);

//             // // for (volatile int t=0; t<100; t++);
//             // // 2. Signal the passthrough to continue the execution
//             // if (return_val == 0){//signal success
//             //     *signal_addr = 24223u;
//             // } else {
//             //     *signal_addr = 105u;  //signal failure
//             // }

//             // myfunc();

//             // nop();
//             // nop();
//             // return_val = BareMetal_SD_WriteLoop(512, ram_address);

//             // // for (volatile int t=0; t<100; t++);
//             // // 2. Signal the passthrough to continue the execution
//             // if (return_val == 0){//signal success
//             //     *signal_addr = 24223u;
//             // } else {
//             //     *signal_addr = 105u;  //signal failure
//             // }

//             // myfunc();

//             // nop();
//             // nop();
//             // return_val = BareMetal_SD_WriteLoop(512, ram_address);

//             // // for (volatile int t=0; t<100; t++);
//             // // 2. Signal the passthrough to continue the execution
//             // if (return_val == 0){//signal success
//             //     *signal_addr = 24223u;
//             // } else {
//             //     *signal_addr = 105u;  //signal failure
//             // }


//             // myfunc();

//             // nop();
//             // nop();
//             // return_val = BareMetal_SD_WriteLoop(512, ram_address);

//             // // for (volatile int t=0; t<100; t++);
//             // // 2. Signal the passthrough to continue the execution
//             // if (return_val == 0){//signal success
//             //     *signal_addr = 24223u;
//             // } else {
//             //     *signal_addr = 105u;  //signal failure
//             // }


//             // myfunc();

//             // nop();
//             // nop();
//             // return_val = BareMetal_SD_WriteLoop(512, ram_address);

//             // // for (volatile int t=0; t<100; t++);
//             // // 2. Signal the passthrough to continue the execution
//             // if (return_val == 0){//signal success
//             //     *signal_addr = 24223u;
//             // } else {
//             //     *signal_addr = 105u;  //signal failure
//             // }


//             // myfunc();

//             // nop();
//             // nop();
//             // return_val = BareMetal_SD_WriteLoop(512, ram_address);

//             // // for (volatile int t=0; t<100; t++);
//             // // 2. Signal the passthrough to continue the execution
//             // if (return_val == 0){//signal success
//             //     *signal_addr = 24223u;
//             // } else {
//             //     *signal_addr = 105u;  //signal failure
//             // }


//             // mynextfunc(return_val);

//             // mynextfunc(return_val); //now, passthrough in future morph the pc and make us get out of this loop dont worry for now

//             // return_val = BareMetal_SD_WriteLoop(512, ram_address);

//             // // for (volatile int t=0; t<100; t++);
//             // // 2. Signal the passthrough to continue the execution
//             // if (return_val == 0){//signal success
//             //     *signal_addr = 24223u;
//             // } else {
//             //     *signal_addr = 105u;  //signal failure
//             // }

//             /* --- END CORRECTED CODE --- */
//         }

//         /* Code below is unreachable due to while(1) loop */
//         while(1);
// }

// #include "usart_test.c"

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
    #ifdef TRAIN_TIMER
		timer_test();
#endif
#ifdef TRAIN_GPIO
		gpio_test();
#endif
#ifdef TRAIN_USART
		usart_test();
#endif
		unsigned int * ram_baddr = 0x20020000;
		for (int i =0; i< 0x100; i++) {
				ram_baddr[i] = i;
		}
		while(1);
}
