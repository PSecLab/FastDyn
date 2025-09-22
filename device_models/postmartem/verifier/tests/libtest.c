#include <stdio.h>
#include <stdint.h>

void qemu_plugin_raise_irq(int irq) {
    printf("Raising the Interrupt with the IRQ: %d\n", irq);
}

void qemu_plugin_timer_alarm(uint64_t timer_fd, uint64_t delay_ns) {
    printf("Starting the timer with the delay %lu and the callback %lu\n", delay_ns, timer_fd);
}

uint64_t qemu_plugin_timer_new_ns(void (*cb)(void *), void *data) {
    printf("Registering the timer\n");
    return 0;
}

int64_t qemu_plugin_get_virtual_timer(void) {
	return 0;
}

uint64_t qemu_plugin_timer_new_period_ns(void (*cb)(void *), void *data, uint64_t period) {
    return 0;
}