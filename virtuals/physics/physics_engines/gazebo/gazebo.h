#ifndef GAZEBO_PHY_H
#define GAZEBO_PHY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../phy.h"

extern phy_backend_t gazebo_backend;

int virtual_gz_altimeter_init(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* GAZEBO_PHY_H */