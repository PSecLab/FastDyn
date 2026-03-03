#ifndef ALTI_H
#define ALTI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the FMU subsystem.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 *
 * @return 0 on success, negative error code on failure.
 */
int virtual_altimeter_init(int argc, char **argv);
#ifdef __cplusplus
}
#endif

#endif /* ALTI_H */
