model Altimeter
  import Modelica.Blocks.Noise;

  parameter Integer rngSeed = 42;
  inner Noise.GlobalSeed globalSeed(fixedSeed=rngSeed);

  // --- Standard Atmosphere Constants ---
  parameter Real P0 = 101325.0;
  parameter Real T0 = 288.15;
  parameter Real g = 9.80665;
  parameter Real L = 0.0065;
  parameter Real R = 8.3144598;
  parameter Real M = 0.0289644;
  parameter Real expo = (g * M) / (R * L);

  // --- Sensor Hardware & Noise Specs ---
  parameter Real maxPressure = 125000.0;
  parameter Real minPressure = 30000.0;
  parameter Real adcResolution = 1048576;  // 20-bit ADC

  // Tuned for a typical MEMS Barometer (e.g., MS5611 at max oversampling)
  parameter Real samplePeriod = 0.01;      // 100Hz typical I2C reading rate
  parameter Real awgnSigma = 1.5;          // ~1.5 Pa RMS white noise (~12cm altitude jitter)
  parameter Real pinkTimeConst = 10.0;     // 10-second thermal/structural drift
  parameter Real spikeProb = 0.005;        // 0.5% chance of a sudden aerodynamic pop per sample
  parameter Real spikeMag = 25.0;          // 25 Pa spike (~2 meters of sudden phantom altitude jump)

  // --- I/O Interfaces ---
  input Real trueAltitude;                 // Clean kinematic altitude (m)
  output Real sensedPressure;              // Messy, quantized digital pressure (Pa)

  // --- Internal States ---
  Real truePressure;
  Real pinkNoise(start=0.0, fixed=true);
  Real impulseNoise;
  Real totalNoise;
  Real clampedPressure;

  // --- Internal PRNG Noise Generators ---
  Noise.NormalNoise awgn(samplePeriod=samplePeriod, mu=0, sigma=awgnSigma);
  Noise.NormalNoise pinkSource(samplePeriod=samplePeriod, mu=0, sigma=awgnSigma);
  
  // FIX: Changed yMin/yMax to y_min/y_max for MSL 4.0.0 compatibility
  Noise.UniformNoise spikeTrigger(samplePeriod=samplePeriod, y_min=0, y_max=1);

equation
  // 1. Physical Plant: Standard atmosphere conversion
  truePressure = P0 * (1.0 - (L * trueAltitude) / T0) ^ expo;

  // 2. Pink Noise (Drift): Low-pass filtering white noise to simulate thermal drift in the silicon
  der(pinkNoise) = (pinkSource.y - pinkNoise) / pinkTimeConst;

  // 3. Impulse Spikes: Simulating sudden wind gusts or ground-effect prop wash hitting the casing
  impulseNoise = if spikeTrigger.y < spikeProb then spikeMag else 0.0;

  // 4. Composite Internal Noise
  totalNoise = awgn.y + pinkNoise + impulseNoise;

  // 5. Hardware Saturation
  clampedPressure = if (truePressure + totalNoise) > maxPressure then maxPressure else
                    if (truePressure + totalNoise) < minPressure then minPressure else 
                    (truePressure + totalNoise);

  // 6. ADC Quantization
  sensedPressure = minPressure + (maxPressure - minPressure) * floor(((clampedPressure - minPressure) / (maxPressure - minPressure)) * adcResolution) / adcResolution;

end Altimeter;
