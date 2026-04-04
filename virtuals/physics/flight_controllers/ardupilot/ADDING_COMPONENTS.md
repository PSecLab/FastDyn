# Adding Components to base level ArduPilot Courbet.

## First case: Adding a 360 degree lidar to the rover model. (Should be applicable to any model)

### 1. Add the sensor to the model SDF file.

[Gazebo Example of Adding Lidar to an existing vehicle](https://gazebosim.org/docs/latest/sensors/)

### 2. Understand the ardupilot library and how it expects to interface with the sensor.

#### a. Find the backend

The desired backend to emulate is: `libraries/AP_Proximity/AP_Proximity_RPLidarA2.cpp`

#### b. Understand how AP_Proximity is initialized and how it tries to initialize backends.

```C++
void AP_Proximity::init()
{
    if (num_instances != 0) {
        // init called a 2nd time?
        return;
    }

    // instantiate backends
    uint8_t serial_instance = 0;
    (void)serial_instance;  // in case no serial backends are compiled in
    for (uint8_t instance=0; instance<AP_PROXIMITY_MAX_INSTANCES; instance++) {
        switch (get_type(instance)) {
        case Type::None:
            break;
#if AP_PROXIMITY_RPLIDARA2_ENABLED
        case Type::RPLidarA2:
            if (AP_Proximity_RPLidarA2::detect(serial_instance)) {
                state[instance].instance = instance;
                drivers[instance] = NEW_NOTHROW AP_Proximity_RPLidarA2(*this, state[instance], params[instance], serial_instance);
                serial_instance++;
            }
            break;
```

This constructor leads to a call to the parent class:

```C++
/*
   The constructor also initialises the proximity sensor. Note that this
   constructor is not called until detect() returns true, so we
   already know that we should setup the proximity sensor
*/
AP_Proximity_Backend_Serial::AP_Proximity_Backend_Serial(AP_Proximity &_frontend,
                                                         AP_Proximity::Proximity_State &_state,
                                                         AP_Proximity_Params &_params,
                                                         uint8_t serial_instance) :
    AP_Proximity_Backend(_frontend, _state, _params)
{
    const AP_SerialManager &serial_manager = AP::serialmanager();
    _uart = serial_manager.find_serial(AP_SerialManager::SerialProtocol_Lidar360, serial_instance);
    if (_uart != nullptr) {
        // start uart with larger receive buffer
        _uart->begin(serial_manager.find_baudrate(AP_SerialManager::SerialProtocol_Lidar360, serial_instance), rxspace(), 0);
    }
}

// static detection function
// detect if a proximity sensor is connected by looking for a configured serial port
// serial_instance affects which serial port is used.  Should be 0 or 1 depending on whether this is the 1st or 2nd proximity sensor with a serial interface
bool AP_Proximity_Backend_Serial::detect(uint8_t serial_instance)
{
    return AP::serialmanager().have_serial(AP_SerialManager::SerialProtocol_Lidar360, serial_instance);
}
```

Since we need a serial line to be configured with the serial protocol for lidar360 we will have to compile a new binary and change the following lines.

```C++
#ifndef DEFAULT_SERIAL5_PROTOCOL
#define DEFAULT_SERIAL5_PROTOCOL SerialProtocol_Lidar360
#endif
#ifndef DEFAULT_SERIAL5_BAUD
#define DEFAULT_SERIAL5_BAUD AP_SERIALMANAGER_GIMBAL_BAUD/1000
#endif
#ifndef DEFAULT_SERIAL5_OPTIONS
#define DEFAULT_SERIAL5_OPTIONS 0
```

I wrote two virtuals to help with the initialization which is selecting the type of lidar and setting the type in the params struct. The backend will be initialized if the type is set to RPLidarA2 and the serial manager has a port configured for lidar360. The virtuals are `proximity_get_type` and `proximity_set_type_param`. The first one is used to select the lidar type and the second is used to set the lidar type in the params struct which is used by the driver to determine how to parse data from the sensor.

#### c. Look at how the driver backend communicates with the device and what format the data is in.

[RPLidarA2 High-Fidelity State Machine / Model](https://chatgpt.com/share/69c80ef4-db98-8330-a8ed-f8ff76dde0af)

#### d. Design a service in the backend which can mimic the way the actual sensor acts.

In our case the lidar rotates at a constant speed and adds readings from each angle to a buffer. When the buffer is full, it would overwrite the oldest data.

In my case every time we read we will just get all of the most recent distances and format 400 samples.

The `gz.msgs.LaserScan` message has 400 samples and a min and max range of 0.15 and 16.0 respectively. The lidar will publish at 10hz so we will have to make sure to publish at that rate. I made this addition to the sdf to add our custom lidar:

```xml
      <sensor name='gpu_lidar' type='gpu_lidar'>
          <pose>0 0 0.4 0 0 0</pose>
          <topic>lidar</topic>
          <update_rate>10</update_rate>

          <ray>
              <scan>
                  <horizontal>
                      <samples>400</samples>
                      <resolution>1</resolution>
                      <min_angle>-3.14159</min_angle>
                      <max_angle>3.14159</max_angle>
                  </horizontal>

                  <vertical>
                      <samples>1</samples>
                      <resolution>0.01</resolution>
                      <min_angle>0</min_angle>
                      <max_angle>0</max_angle>
                  </vertical>
              </scan>

              <range>
                  <min>0.15</min>
                  <max>16.0</max>
                  <resolution>0.01</resolution>
              </range>
          </ray>

          <always_on>1</always_on>
          <visualize>true</visualize>
      </sensor>
```

We can make a service that returns a variable number of samples as an array of tuples for angle and distance. The service will be called `get_lidar_scan` and will return the most recent scan from the lidar.

#### e. Now, that you have a model, you can map hook points in the driver to our model.

