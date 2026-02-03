#include <math.h>
#include <stdio.h>

const double PI = 3.14159265359;
const float PI_F = 3.14159265359f;

#define DEG_TO_RAD_F      (PI_F / 180.0f)
#define DEG_TO_RAD_D      (PI / 180.0)
#define RAD_TO_DEG_F      (1.0f / DEG_TO_RAD_F)
#define RAD_TO_DEG_D      (1.0 / DEG_TO_RAD_D)
#define GRAVITY_MSS_F     9.80665f
#define GRAVITY_MSS_D     9.80665

float degF_to_Kelvin_f(float temp_f) { return (temp_f + 459.67f) * 0.55556F; }
// double degF_to_Kelvin_d(double temp_f) { return (temp_f + 459.67) * 0.55556; }

float radians_f(float deg) { return deg * DEG_TO_RAD_F; }
float degrees_f(float rad) { return rad * RAD_TO_DEG_F; }

// double radians_d(double deg) { return deg * DEG_TO_RAD_D; }
// double degrees_d(double rad) { return rad * RAD_TO_DEG_D; }

float sq_f(const float v) { return v*v; }
// double sq_d(const double v) { return v*v; }

// double w_d(const double dHertz) { return dHertz * 2.0 * PI; }
float w_f(const float dHertz) { return dHertz * 2.0f * PI_F; }

float angle_to_accel_f(float angle_deg) { return GRAVITY_MSS_F * tanf(angle_deg*DEG_TO_RAD_F); }
float accel_to_angle_f(float accel) { return atanf(accel/GRAVITY_MSS_F)*RAD_TO_DEG_F; }
// double angle_to_accel_d(double angle_deg) { return GRAVITY_MSS_D * tan(angle_deg*DEG_TO_RAD_D); }
// double accel_to_angle_d(double accel) { return atan(accel/GRAVITY_MSS_D)*RAD_TO_DEG_D; }

#define SQRT_2_3_F 0.816496580927726f
#define SQRT_6_F   2.449489742783178f
#define SQRT_2_3_D 0.816496580927726
#define SQRT_6_D   2.449489742783178
static const float TAU_FACTOR_F = SQRT_6_F / 24.0f;
// static const double TAU_FACTOR_D = SQRT_6_D / 24.0;

// Helper function used for Quinn's frequency estimation
float tau_f(const float x)
{
    float p1 = logf(3.0f * x*x + 6.0f * x + 1.0f);
    float part1 = x + 1.0f - SQRT_2_3_F;
    float part2 = x + 1.0f + SQRT_2_3_F;
    float p2 = logf(part1 / part2);
    return (0.25f * p1 - TAU_FACTOR_F * p2);
}

// // Helper function used for Quinn's frequency estimation
// double tau_d(const double x)
// {
//     double p1 = log(3.0 * x*x + 6.0 * x + 1.0);
//     double part1 = x + 1.0 - SQRT_2_3_D;
//     double part2 = x + 1.0 + SQRT_2_3_D;
//     double p2 = log(part1 / part2);
//     return (0.25 * p1 - TAU_FACTOR_D * p2);
// }


// int main() {
//     float xf;
//     // double xd;
//     int l;

//     l = scanf("%f", &xf);
//     // l = scanf("%lf", &xd);

//     printf("%f\n", degF_to_Kelvin_f(xf));
//     // printf("%lf\n", degF_to_Kelvin_d(xd));

//     printf("%f\n", radians_f(xf));
//     // printf("%lf\n", radians_d(xd));

//     printf("%f\n", degrees_f(xf));
//     // printf("%lf\n", degrees_d(xd));

//     printf("%f\n", w_f(xf));
//     // printf("%lf\n", w_d(xd));

//     printf("%f\n", angle_to_accel_f(xf));
//     // printf("%lf\n", angle_to_accel_d(xd));

//     printf("%f\n", accel_to_angle_f(xf));
//     // printf("%lf\n", accel_to_angle_d(xd));

//     printf("%f\n", tau_f(xf));
//     // printf("%lf\n", tau_d(xd));

//     printf("%f\n", sq_f(xf));
//     // printf("%lf\n", sq_d(xd));
// }
