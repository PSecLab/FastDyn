#include <math.h>
#include <stdio.h>

float fresnelReflectanceAtNormal_f(float index) {
  float partial = (1.0f - index) / (1.0f + index);
  return partial * partial;
}

// double fresnelReflectanceAtNormal_d(double index) {
//   double partial = (1.0 - index) / (1.0 + index);
//   return partial * partial;
// }

float blinToBeckmann_f(float alpha) {
  return sqrtf(2.0f / (alpha + 2.0f));
}

float beckmannToBlinn_f(float slope) {
  return 2.0f / (slope * slope) - 2.0f;
}

// double blinToBeckmann_d(double alpha) {
//   return sqrtf(2.0 / (alpha + 2.0));
// }

// double beckmannToBlinn_d(double slope) {
//   return 2.0 / (slope * slope) - 2.0;
// }

// int main() {
//     float xf;
//     // double xd;
//     int l;

//     l = scanf("%f", &xf);
//     // l = scanf("%lf", &xd);

//     printf("%f\n",  fresnelReflectanceAtNormal_f(xf));
//     // printf("%lf\n", fresnelReflectanceAtNormal_d(xd));

//     printf("%f\n",  blinToBeckmann_f(xf));
//     // printf("%lf\n", blinToBeckmann_d(xd));

//     printf("%f\n",  beckmannToBlinn_f(xf));
//     // printf("%lf\n", beckmannToBlinn_d(xd));
// }
