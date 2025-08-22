extern DeviceModel passthrough_model_def;
extern DeviceModel elder_model_def;
extern DeviceModel classic_model_def;

// The global, statically-defined array of device models.
static DeviceModel *all_devices[] = {
	&passthrough_model_def,
	&elder_model_def,
	&classic_model_def
};

// Calculate the number of devices at compile time.
static const int num_devices = sizeof(all_devices) / sizeof(all_devices[0]);
