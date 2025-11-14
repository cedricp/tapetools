#pragma once

#define _USE_MATH_DEFINES

#include <math.h>
#include <vector>


#define FFTW_REAL_INDEX 0
#define FFTW_IMAGINARY_INDEX 1

void smoothed_z_score(const double y[], double signals[], const int count, const int lag, const float threshold, const float influence);
bool sg_smooth(const double *v, double *res, const int size, const int width, const int deg);

static double zerocross(double a[2], double b[2])
{
	double a1 = b[0] - a[0];
	double b1 = b[1] - a[1];
	double slope = b1 / a1;
	double c = (slope * a[0]) - a[1];
	return c / slope;
}

static double wrap_phase(const double x) {
    if (x >= M_PI)       return x - 2.0*M_PI;
    else if (x <= -M_PI) return x + 2.0*M_PI;
    else                 return x;
};

static double rectangle_fft_window(int i, int length)
{
	return 1.0;
}

static double hamming_fft_window(int i, int length)
{
	double a, b, w, N1;
	a = 25.0/46.0;
	b = 21.0/46.0;
	N1 = (double)(length-1);
	w = a - b*cos(2*i*M_PI/N1);
	return w;
}

static double hann_fft_window(int i, int length)
{
    return 0.5f*(1.0f-cos(2.0f*M_PI*(double)(i)/(double)(length-1.0f)));
}

static double hann_poisson_fft_window(int i, int length)
{
	double a, N1, w;
	a = 2.0;
	N1 = (double)(length-1);
	w = 0.5 * (1 - cos(2*M_PI*i/N1)) * \
	    pow(M_E, (-a*(double)abs((int)(N1-1-2*i)))/N1);
	return w;
}

static double blackman_fft_window(int i, int length)
{
	double a0, a1, a2, w, N1;
	a0 = 7938.0/18608.0;
	a1 = 9240.0/18608.0;
	a2 = 1430.0/18608.0;
	N1 = (double)(length-1);
	w = a0 - a1*cos(2*i*M_PI/N1) + a2*cos(4*i*M_PI/N1);
	return w;
}

static double blackman_harris_fft_window(int i, int length)
{
	double a0, a1, a2, a3, w, N1;
	a0 = 0.35875;
	a1 = 0.48829;
	a2 = 0.14128;
	a3 = 0.01168;
	N1 = (double)(length-1);
	w = a0 - a1*cos(2*i*M_PI/N1) + a2*cos(4*i*M_PI/N1) - a3*cos(6*i*M_PI/N1);
	return w;
}

static double youssef_fft_window(int i, int length)
/* really a blackman-harris-poisson window, but that is a mouthful */
{
	double a, a0, a1, a2, a3, w, N1;
	a0 = 0.35875;
	a1 = 0.48829;
	a2 = 0.14128;
	a3 = 0.01168;
	N1 = (double)(length-1);
	w = a0 - a1*cos(2*i*M_PI/N1) + a2*cos(4*i*M_PI/N1) - a3*cos(6*i*M_PI/N1);
	a = 0.0025;
	w *= pow(M_E, (-a*(double)abs((int)(N1-1-2*i)))/N1);
	return w;
}

static double bartlett_fft_window(int i, int length)
{
	double N1, L, w;
	L = (double)length;
	N1 = L - 1;
	w = (i - N1/2) / (L/2);
	if (w < 0) {
		w = -w;}
	w = 1 - w;
	return w;
}

static double zeroethOrderBessel(double x)
{
	const double eps = 0.000001f;

	//  initialize the series term for m=0 and the result
	double besselValue = 0;
	double term = 1;
	double m = 0;

	//  accumulate terms as long as they are significant
	while(term  > eps * besselValue){
		besselValue += term;
			
		//  update the term
		++m;
		term *= (x*x) / (4*m*m);
	}
	return besselValue;
}

static inline double kaiser_fft_window(double shape, int i,int len)
{   
     //  Pre-compute the shared denominator in the Kaiser equation. 
	const double oneOverDenom = 1.0 / zeroethOrderBessel( shape );
	const unsigned int N = len - 1;
	const double oneOverN = 1.0 / N;
	const double K = (2.0 * (double)(i) * oneOverN) - 1.0;
	const double arg = sqrt( 1.0 - (K * K) );        
	return zeroethOrderBessel(shape * arg) * oneOverDenom;
}


// Improved Kaiser version 
static inline double bessel_i0_fast(double x) {
    double ax = fabs(x);
    if (ax < 3.75) {
        double y = x / 3.75, y2 = y * y;
        return 1.0 + y2 * (3.5156229 + y2 * (3.0899424 + y2 * (1.2067492
               + y2 * (0.2659732 + y2 * (0.0360768 + y2 * 0.0045813)))));
    } else {
        double y = 3.75 / ax;
        return (exp(ax) / sqrt(ax)) *
               (0.39894228 + y * (0.01328592 + y * (0.00225319
               + y * (-0.00157565 + y * (0.00916281
               + y * (-0.02057706 + y * (0.02635537
               + y * (-0.01647633 + y * 0.00392377))))))));
    }
}

static inline double bessel_i0(double x) {
    const double eps = 1e-12;
    double sum = 1.0, term = 1.0;
    double m = 1.0;
    while (fabs(term) > eps * fabs(sum)) {
        term *= (x * x) / (4.0 * m * m);
        sum += term;
        ++m;
    }
    return sum;
}

static inline double kaiser2_fft_window(double beta, int i, int len) {
    const int N = len - 1;
    const double denom = bessel_i0_fast(beta);
    const double ratio = (2.0 * i) / (double)N - 1.0;
    const double arg = sqrt(1.0 - ratio * ratio);
    return bessel_i0(beta * arg) / denom;
}

static double kaiser6_fft_window(int i,int len){
	return kaiser2_fft_window(5.f, i, len);
}

static inline double complex_module(double i, double r)
{
    return sqrt(i*i+r*r);
}

static inline double complex_argument(double i, double r)
{
	return atan2(i, r);
}
