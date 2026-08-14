#ifndef PROTOCOL_FUZZERS_H
#define PROTOCOL_FUZZERS_H

void fuzz_packetreceived_inject(void);
void fuzz_callback();
void generic_configure(int argc, char **argv);
void generic_callback(void);

#endif
