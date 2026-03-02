model RealisticTempSensor
  import Modelica.Blocks.Noise;

  // 1. Explicit Physical Parameters
  parameter Real ambientTemp = 298.15;     // 25°C in Kelvin
  parameter Real C = 0.01;                 // Thermal capacitance (J/K)
  parameter Real R = 50.0;                 // Thermal resistance (K/W)
  parameter Real selfHeatingPower = 0.002; // Parasitic I^2R heating from the measurement circuit (W)

  // 2. Sensor Operating Limits (Saturation)
  parameter Real maxTemp = 398.15;         // 125°C maximum measurable limit
  parameter Real minTemp = 233.15;         // -40°C minimum measurable limit

  // 3. ADC Parameters (Quantization)
  parameter Real adcResolution = 4096;     // 12-bit ADC

  // I/O
  input Real heatSource;                   // External heat applied (W)
  output Real sensedTemp;                  // The final digitized value the microcontroller sees
  
  // Internal states for debugging and physics
  Real T(start=ambientTemp, fixed=true);   // The true physical temperature
  Real noisyTemp;                          // Temperature with electromagnetic/thermal noise
  Real clampedTemp;                        // Temperature bound by sensor hardware limits

equation
  // A. Physical Thermal Dynamics (Lumped Capacitance)
  // Incorporates thermal inertia, thermal resistance to ambient, and parasitic self-heating
  C * der(T) = heatSource + selfHeatingPower - (T - ambientTemp) / R;

  // B. Noise Injection
  // Adds a simple high-frequency sinusoidal noise to approximate environmental interference. 
  // In a complex system, you might replace this with Modelica.Blocks.Noise.NormalNoise.
  noisyTemp = T + 0.5 * sin(time * 1000); 

  // C. Sensor Saturation
  // The physical hardware cannot read values beyond its specified limits
  clampedTemp = if noisyTemp > maxTemp then maxTemp else
                if noisyTemp < minTemp then minTemp else noisyTemp;

  // D. ADC Quantization
  // Simulates what a flight controller or PLC actually reads by stepping the continuous value
  // into discrete bins based on a 12-bit resolution across the sensor's range.
  sensedTemp = minTemp + (maxTemp - minTemp) * floor(((clampedTemp - minTemp) / (maxTemp - minTemp)) * adcResolution) / adcResolution;

end RealisticTempSensor;
