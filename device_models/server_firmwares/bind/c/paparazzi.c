#include <math.h>
#include <stdio.h>

#define PI_D 3.14159265359
#define PI_F 3.14159265359f

float isometric_latitude0_f(float phi) {
  return logf (tanf (PI_F/4.0f + phi / 2.0f));
}
// double isometric_latitude0_d(double phi) {
//   return log (tan (PI_D/4.0 + phi / 2.0));
// }


// Standard Atmosphere constants
/** ISA sea level standard atmospheric pressure in Pascal */
#define PPRZ_ISA_SEA_LEVEL_PRESSURE_F 101325.0f
#define PPRZ_ISA_SEA_LEVEL_PRESSURE_D 101325.0
/** ISA sea level standard temperature in Kelvin */
#define PPRZ_ISA_SEA_LEVEL_TEMP_F 288.15f
#define PPRZ_ISA_SEA_LEVEL_TEMP_D 288.15
/** temperature laps rate in K/m */
#define PPRZ_ISA_TEMP_LAPS_RATE_F 0.0065f
#define PPRZ_ISA_TEMP_LAPS_RATE_D 0.0065
/** earth-surface gravitational acceleration in m/s^2 */
#define PPRZ_ISA_GRAVITY_F 9.80665f
#define PPRZ_ISA_GRAVITY_D 9.80665
/** universal gas constant in J/(mol*K) */
#define PPRZ_ISA_GAS_CONSTANT_F 8.31447f
#define PPRZ_ISA_GAS_CONSTANT_D 8.31447
/** molar mass of dry air in kg/mol */
#define PPRZ_ISA_MOLAR_MASS_F 0.0289644f
#define PPRZ_ISA_MOLAR_MASS_D 0.0289644
/** universal gas constant / molar mass of dry air in J*kg/K */
#define PPRZ_ISA_AIR_GAS_CONSTANT_F (PPRZ_ISA_GAS_CONSTANT_F/PPRZ_ISA_MOLAR_MASS_F)
#define PPRZ_ISA_AIR_GAS_CONSTANT_D (PPRZ_ISA_GAS_CONSTANT_D/PPRZ_ISA_MOLAR_MASS_D)
/** standard air density in kg/m^3 */
#define PPRZ_ISA_AIR_DENSITY_F 1.225f
#define PPRZ_ISA_AIR_DENSITY_D 1.225

static const float PPRZ_ISA_M_OF_P_CONST_F = (PPRZ_ISA_AIR_GAS_CONSTANT_F * PPRZ_ISA_SEA_LEVEL_TEMP_F / PPRZ_ISA_GRAVITY_F);
// static const double PPRZ_ISA_M_OF_P_CONST_D = (PPRZ_ISA_AIR_GAS_CONSTANT_D * PPRZ_ISA_SEA_LEVEL_TEMP_D / PPRZ_ISA_GRAVITY_D);

float pprz_isa_pressure_of_altitude_f(float altitude)
{
  return (PPRZ_ISA_SEA_LEVEL_PRESSURE_F * expf((-1.0f / PPRZ_ISA_M_OF_P_CONST_F) * altitude));
}
// double pprz_isa_pressure_of_altitude_d(double altitude)
// {
//   return (PPRZ_ISA_SEA_LEVEL_PRESSURE_D * exp((-1. / PPRZ_ISA_M_OF_P_CONST_D) * altitude));
// }

float pprz_isa_altitude_of_pressure_f(float pressure)
{
  // if (pressure > 0.) {
    return (PPRZ_ISA_M_OF_P_CONST_F * logf(PPRZ_ISA_SEA_LEVEL_PRESSURE_F / pressure));
  // } else {
    // return 0.;
  // }
}

// double pprz_isa_altitude_of_pressure_d(double pressure)
// {
//   // if (pressure > 0.) {
//     return (PPRZ_ISA_M_OF_P_CONST_D * log(PPRZ_ISA_SEA_LEVEL_PRESSURE_D / pressure));
//   // } else {
//     // return 0.;
//   // }
// }

float pprz_isa_pressure_of_height_f(float height)
{
    float ref_p = 0.8f * PPRZ_ISA_SEA_LEVEL_PRESSURE_F;
  return (ref_p * expf((-1.0f / PPRZ_ISA_M_OF_P_CONST_F) * height));
}

// double pprz_isa_pressure_of_height_d(double height)
// {
//     double ref_p = 0.8 * PPRZ_ISA_SEA_LEVEL_PRESSURE_D;
//   return (ref_p * exp((-1.0 / PPRZ_ISA_M_OF_P_CONST_D) * height));
// }

float pprz_isa_height_of_pressure_full_f(float pressure)
{
    const float ref_p = 0.8f * PPRZ_ISA_SEA_LEVEL_PRESSURE_F;
    const float prel = pressure / ref_p;
    const float inv_expo = PPRZ_ISA_GAS_CONSTANT_F * PPRZ_ISA_TEMP_LAPS_RATE_F /
                           PPRZ_ISA_GRAVITY_F / PPRZ_ISA_MOLAR_MASS_F;
    return (1.0f - powf(prel, inv_expo)) * PPRZ_ISA_SEA_LEVEL_TEMP_F / PPRZ_ISA_TEMP_LAPS_RATE_F;
}

// double pprz_isa_height_of_pressure_full_d(double pressure)
// {
//     const double ref_p = 0.8 * PPRZ_ISA_SEA_LEVEL_PRESSURE_D;
//     const double prel = pressure / ref_p;
//     const double inv_expo = PPRZ_ISA_GAS_CONSTANT_D * PPRZ_ISA_TEMP_LAPS_RATE_D /
//                            PPRZ_ISA_GRAVITY_D / PPRZ_ISA_MOLAR_MASS_D;
//     return (1.0 - pow(prel, inv_expo)) * PPRZ_ISA_SEA_LEVEL_TEMP_D / PPRZ_ISA_TEMP_LAPS_RATE_D;
// }

float pprz_isa_height_of_pressure_f(float pressure)
{
    const float ref_p = 0.8f * PPRZ_ISA_SEA_LEVEL_PRESSURE_F;
    return (PPRZ_ISA_M_OF_P_CONST_F * logf(ref_p / pressure));
}
// double pprz_isa_height_of_pressure_d(double pressure)
// {
//     const double ref_p = 0.8 * PPRZ_ISA_SEA_LEVEL_PRESSURE_D;
//     return (PPRZ_ISA_M_OF_P_CONST_D * log(ref_p / pressure));
// }

float pprz_isa_temperature_of_altitude_f(float alt)
{
  return PPRZ_ISA_SEA_LEVEL_TEMP_F - PPRZ_ISA_TEMP_LAPS_RATE_F * alt;
}
// double pprz_isa_temperature_of_altitude_d(double alt)
// {
//   return PPRZ_ISA_SEA_LEVEL_TEMP_D - PPRZ_ISA_TEMP_LAPS_RATE_D * alt;
// }

float eas_from_dynamic_pressure_f(float q)
{
  const float two_div_rho_0 = 2.0f / PPRZ_ISA_AIR_DENSITY_F;
  return sqrtf(q * two_div_rho_0);
}

// double eas_from_dynamic_pressure_d(double q)
// {
//   const double two_div_rho_0 = 2.0 / PPRZ_ISA_AIR_DENSITY_D;
//   return sqrt(q * two_div_rho_0);
// }

float change_rep_f(float dir)
{
  return PI_F/2.0f - dir;
}
// double change_rep_d(double dir)
// {
//   return PI_D/2.0 - dir;
// }

#define NMEA_PI180_F                  (PI_F / 180.0f)
#define NMEA_PI180_D                  (PI_D / 180.0)
float nmea_degree2radian_f(float val) { return (val * NMEA_PI180_F); }
// double nmea_degree2radian_d(double val) { return (val * NMEA_PI180_D); }
float nmea_radian2degree_f(float val) { return (val / NMEA_PI180_F); }
// double nmea_radian2degree_d(double val) { return (val / NMEA_PI180_D); }

// int main() {
//     float xf;
//     // double xd;
//     int l;

//     l = scanf("%f", &xf);
//     // l = scanf("%lf", &xd);

//     printf("%f\n", isometric_latitude0_f(xf));
//     // printf("%lf\n", isometric_latitude0_d(xd));

//     printf("%f\n", pprz_isa_pressure_of_altitude_f(xf));
//     // printf("%lf\n", pprz_isa_pressure_of_altitude_d(xd));

//     printf("%f\n", pprz_isa_altitude_of_pressure_f(xf));
//     // printf("%lf\n", pprz_isa_altitude_of_pressure_d(xd));

//     printf("%f\n", pprz_isa_pressure_of_height_f(xf));
//     // printf("%lf\n", pprz_isa_pressure_of_height_d(xd));

//     printf("%f\n", pprz_isa_height_of_pressure_full_f(xf));
//     // printf("%lf\n", pprz_isa_height_of_pressure_full_d(xd));

//     printf("%f\n", pprz_isa_height_of_pressure_f(xf));
//     // printf("%lf\n", pprz_isa_height_of_pressure_d(xd));

//     printf("%f\n", pprz_isa_temperature_of_altitude_f(xf));
//     // printf("%lf\n", pprz_isa_temperature_of_altitude_d(xd));

//     printf("%f\n", eas_from_dynamic_pressure_f(xf));
//     // printf("%lf\n", eas_from_dynamic_pressure_d(xd));

//     printf("%f\n", change_rep_f(xf));
//     // printf("%lf\n", change_rep_d(xd));

//     printf("%f\n", nmea_degree2radian_f(xf));
//     // printf("%lf\n", nmea_degree2radian_d(xd));

//     printf("%f\n", nmea_radian2degree_f(xf));
//     // printf("%lf\n", nmea_radian2degree_d(xd));
// }
