#include <math.h>
#include <stdio.h>

float calcAltitude_f(float pressure)
{
  float A = pressure/101325.0f;
  float B = 1.0f/5.25588f;
  float C = powf(A,B);
  C = 1.0f - C;
  C = C /0.0000225577f;
  return C;
}

float convertCtoF_f(float c) {
    return c * 9.0f / 5.0f + 32.0f;
}

// double calcAltitude_d(double pressure)
// {
//   double A = pressure/101325.0;
//   double B = 1.0/5.25588;
//   double C = pow(A,B);
//   C = 1.0 - C;
//   C = C /0.0000225577;
//   return C;
// }

// double convertCtoF_d(double c) {
//     return c * 9.0f / 5.0f + 32.0f;
// }

// int main() {
//     float xf;
//     // double xd;
//     int l;

//     l = scanf("%f", &xf);
//     // l = scanf("%lf", &xd);

//     printf("%f\n", calcAltitude_f(xf));
//     // printf("%lf\n", calcAltitude_d(xd));

//     printf("%f\n", convertCtoF_f(xf));
//     // printf("%lf\n", convertCtoF_d(xd));
// }
