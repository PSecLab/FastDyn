#include <math.h>
#include <stdio.h>

#define M_PIf       3.14159265358979323846f
#define sinPolyCoef3_f -1.666665710e-1f                                          // Double: -1.666665709650470145824129400050267289858e-1
#define sinPolyCoef5_f  8.333017292e-3f                                          // Double:  8.333017291562218127986291618761571373087e-3
#define sinPolyCoef7_f -1.980661520e-4f                                          // Double: -1.980661520135080504411629636078917643846e-4
#define sinPolyCoef9_f  2.600054768e-6f                                          // Double:  2.600054767890361277123254766503271638682e-6
#define sinPolyCoef3_d -1.666665709650470145824129400050267289858e-1
#define sinPolyCoef5_d  8.333017291562218127986291618761571373087e-3
#define sinPolyCoef7_d -1.980661520135080504411629636078917643846e-4
#define sinPolyCoef9_d  2.600054767890361277123254766503271638682e-6


float invSqrt_f(float x) { return 1.0f / sqrtf(x); }
// double invSqrt_d(double x) { return 1.0 / sqrt(x); }

float pressureToAltitude_f(const float pressure) { return (1.0f - powf(pressure / 101325.0f, 0.190295f)) * 4433000.0f; }
// double pressureToAltitude_d(const double pressure) { return (1.0 - pow(pressure / 101325.0, 0.190295)) * 4433000.0; }

float dynThrottle_f(float throttle) { return throttle * (1.0f - (throttle * throttle) / 3.0f) * 1.5f; }
// double dynThrottle_d(double throttle) { return throttle * (1.0 - (throttle * throttle) / 3.0) * 1.5; }

float calculateAccZLowPassFilterRCTimeConstant_f(float accz_lpf_cutoff) { return 0.5f / (M_PIf * accz_lpf_cutoff); }
// double calculateAccZLowPassFilterRCTimeConstant_d(double accz_lpf_cutoff) { return 0.5 / (M_PI * accz_lpf_cutoff); }

float calculateThrottleAngleScale_f(float throttle_correction_angle) { return (1800.0f / M_PIf) * (900.0f / throttle_correction_angle); }
// double calculateThrottleAngleScale_d(double throttle_correction_angle) { return (1800.0 / M_PI) * (900.0 / throttle_correction_angle); }

float sin_approx_f(float x)
{
    float x2 = x * x;
    return x + x * x2 * (sinPolyCoef3_f + x2 * (sinPolyCoef5_f + x2 * (sinPolyCoef7_f + x2 * sinPolyCoef9_f)));
}
// double sin_approx_d(double x)
// {
//     double x2 = x * x;
//     return x + x * x2 * (sinPolyCoef3_d + x2 * (sinPolyCoef5_d + x2 * (sinPolyCoef7_d + x2 * sinPolyCoef9_d)));
// }

float acos_approx_f(float xa)
{
    return sqrtf(1.0f - xa) * (1.5707288f + xa * (-0.2121144f + xa * (0.0742610f + (-0.0187293f * xa))));
}
// double acos_approx_d(double xa)
// {
//     return sqrt(1.0 - xa) * (1.5707288 + xa * (-0.2121144 + xa * (0.0742610 + (-0.0187293 * xa))));
// }

// int main() {
//     float xf;
//     // double xd;
//     int l;

//     l = scanf("%f", &xf);
//     // l = scanf("%lf", &xd);

//     printf("%f\n", invSqrt_f(xf));
//     // printf("%lf\n", invSqrt_d(xd));

//     printf("%f\n", pressureToAltitude_f(xf));
//     // printf("%lf\n", pressureToAltitude_d(xd));

//     printf("%f\n", dynThrottle_f(xf));
//     // printf("%lf\n", dynThrottle_d(xd));

//     printf("%f\n", calculateAccZLowPassFilterRCTimeConstant_f(xf));
//     // printf("%lf\n", calculateAccZLowPassFilterRCTimeConstant_d(xd));

//     printf("%f\n", calculateThrottleAngleScale_f(xf));
//     // printf("%lf\n", calculateThrottleAngleScale_d(xd));

//     printf("%f\n", sin_approx_f(xf));
//     // printf("%lf\n", sin_approx_d(xd));

//     printf("%f\n", acos_approx_f(xf));
//     // printf("%lf\n", acos_approx_d(xd));
// }
