#define REFERENCE_RESISTANCE_F   8000.0f
#define NOMINAL_RESISTANCE_F     100000.0f
#define NOMINAL_TEMPERATURE_F    25.0f
#define B_VALUE_F                3950.0f

#define REFERENCE_RESISTANCE_D   8000.0
#define NOMINAL_RESISTANCE_D     100000.0
#define NOMINAL_TEMPERATURE_D    25.0
#define B_VALUE_D                3950.0

#include <math.h>
#include <stdio.h>

// double resistanceToKelvins_d(double resistance) {
// 	double inverseKelvin = 1.0 / NOMINAL_TEMPERATURE_D +
// 		log(resistance / NOMINAL_RESISTANCE_D) / B_VALUE_D;
// 	return (1.0 / inverseKelvin);
// }

// double readResistance_d(double voltage) {
// 	return REFERENCE_RESISTANCE_D / (1024.0 / voltage - 1.0);
// }

// double celsiusToKelvins_d(double celsius) {
// 	return (celsius + 273.15);
// }

// double kelvinsToCelsius_d(double kelvins) {
// 	return (kelvins - 273.15);
// }

// double celsiusToFahrenheit_d(double celsius) {
// 	return (celsius * 1.8 + 32.0);
// }

// double kelvinsToFahrenheit_d(double kelvins) {
// 	return (kelvins - 273.15) * 1.8 + 32.0;
// }

float resistanceToKelvins_f(float resistance) {
	float inverseKelvin = 1.0f / NOMINAL_TEMPERATURE_F +
		logf(resistance / NOMINAL_RESISTANCE_F) / B_VALUE_F;
	return (1.0f / inverseKelvin);
}

float readResistance_f(float voltage) {
	return REFERENCE_RESISTANCE_F / (1024.0f / voltage - 1.0f);
}

float celsiusToKelvins_f(float celsius) {
	return (celsius + 273.15f);
}

float kelvinsToCelsius_f(float kelvins) {
	return (kelvins - 273.15f);
}

float celsiusToFahrenheit_f(float celsius) {
	return (celsius * 1.8f + 32.0f);
}

float kelvinsToFahrenheit_f(float kelvins) {
	return (kelvins - 273.15f) * 1.8f + 32.0f;
}

// int main() {
//     float xf;
//     // double xd;
//     int l;

//     l = scanf("%f", &xf);
//     // l = scanf("%lf", &xd);

//     printf("%f\n", resistanceToKelvins_f(xf));
//     // printf("%lf\n", resistanceToKelvins_d(xd));

//     printf("%f\n", celsiusToKelvins_f(xf));
//     // printf("%lf\n", celsiusToKelvins_d(xd));

//     printf("%f\n", readResistance_f(xf));
//     // printf("%lf\n", readResistance_d(xd));

//     printf("%f\n", kelvinsToCelsius_f(xf));
//     // printf("%lf\n", kelvinsToCelsius_d(xd));

//     printf("%f\n", kelvinsToFahrenheit_f(xf));
//     // printf("%lf\n", kelvinsToFahrenheit_d(xd));

//     printf("%f\n", celsiusToFahrenheit_f(xf));
//     // printf("%lf\n", celsiusToFahrenheit_d(xd));

// }
