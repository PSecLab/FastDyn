#include <virtuals.h>
#include "inspct.h"

//TODO: Fix this and make it modular
extern int inspct_freertos_init(int, char**);
extern int inspct_chibios_init(int, char**);

int inspct_init(int argc, char ** argv, const char *schema_path) {
		load_fastdyn_schemas(schema_path);
		//TODO: Initialize appropriately, we will need to have the OS as part of the arguments sent here.
		inspct_freertos_init(argc, argv);

		inspct_chibios_init(argc, argv);

		return 0;
}
