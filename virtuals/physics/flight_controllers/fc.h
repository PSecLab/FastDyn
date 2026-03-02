#ifndef FC_H
#define FC_H

/**
 * @brief Register flight controller specific virtual instructions.
 *
 * This function is called during initialization to register the virtual instructions
 * specific to the flight controller. Each virtual instruction is registered with a unique name
 * and a corresponding callback function that implements its behavior.
 */
int fc_init(int argc, char **argv);

#endif /* FC_H */
