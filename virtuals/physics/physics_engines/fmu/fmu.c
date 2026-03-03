#include "fmu.h"
#include "hifi_altimeter/harness/altimeter.h"

int fmu_init(int argc, char **argv) {
		//TODO: do a better job for initialization
		return virtual_altimeter_init(argc, argv);
}
