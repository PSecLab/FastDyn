// syscalls.c - minimal newlib syscall stubs for bare-metal STM32
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

// Linker provides these (common names in STM32 ld scripts).
// If your script uses different names, adjust accordingly.
extern char _end;     // end of .bss / start of heap
extern char _estack;  // top of stack (end of RAM)

static char *heap_end;

caddr_t _sbrk(int incr) {
    if (heap_end == 0)
        heap_end = &_end;

    char *prev = heap_end;
    char *next = heap_end + incr;

    // Simple heap/stack collision check (heap grows up, stack grows down).
    // Leave a little safety margin (optional).
    if (next >= (char*)&_estack) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }

    heap_end = next;
    return (caddr_t)prev;
}

// The rest can be dummies if you don't need I/O:
int _write(int fd, const void *buf, size_t cnt) { (void)fd; (void)buf; return (int)cnt; }
int _read(int fd, void *buf, size_t cnt) { (void)fd; (void)buf; (void)cnt; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int ptr, int dir) { (void)fd; (void)ptr; (void)dir; return 0; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
void _exit(int status) { (void)status; while (1) {} }