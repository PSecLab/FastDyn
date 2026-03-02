#ifndef FMU_H
#define FMU_H

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
int fmu_init(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* FMU_H */
