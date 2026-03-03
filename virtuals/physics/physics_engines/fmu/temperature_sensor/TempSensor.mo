model TempSensor
  parameter Real ambientTemp = 298.15;
  input Real heatSource;
  output Real sensedTemp;
  
  // Use a local state variable for the physics
  Real T(start=ambientTemp, fixed=true);

equation
  // Clean differential equation
  der(T) = heatSource - (T - ambientTemp);
  
  // Assign the state to the output
  sensedTemp = T;
end TempSensor;
