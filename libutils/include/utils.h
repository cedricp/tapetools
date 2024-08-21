#pragma once

#include <vector>

void smoothed_z_score(const double y[], double signals[], const int count, const int lag, const float threshold, const float influence);
bool sg_smooth(const double *v, double *res, const int size, const int width, const int deg);

double zerocross(double a[2], double b[2])
{
	double a1 = b[0] - a[0];
	double b1 = b[1] - a[1];
	double slope = b1 / a1;
	double c = -(slope * a[0]) + a[1];
	return -c / slope;
}

double rectangle_fft_window(int i, int length)
{
	return 1.0;
}

double hamming_fft_window(int i, int length)
{
	double a, b, w, N1;
	a = 25.0/46.0;
	b = 21.0/46.0;
	N1 = (double)(length-1);
	w = a - b*cos(2*i*M_PI/N1);
	return w;
}

double hann_fft_window(int i, int length)
{
    return 0.5f*(1.0f-cosf(2.0f*M_PI*(double)(i)/(double)(length-1.0f)));
}

double hann_poisson_fft_window(int i, int length)
{
	double a, N1, w;
	a = 2.0;
	N1 = (double)(length-1);
	w = 0.5 * (1 - cosf(2*M_PI*i/N1)) * \
	    pow(M_E, (-a*(double)abs((int)(N1-1-2*i)))/N1);
	return w;
}

double blackman_fft_window(int i, int length)
{
	double a0, a1, a2, w, N1;
	a0 = 7938.0/18608.0;
	a1 = 9240.0/18608.0;
	a2 = 1430.0/18608.0;
	N1 = (double)(length-1);
	w = a0 - a1*cos(2*i*M_PI/N1) + a2*cos(4*i*M_PI/N1);
	return w;
}

double blackman_harris_fft_window(int i, int length)
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

static double kaiser5_fft_window(int i,int len){
	return kaiser_fft_window(5.f, i, len);
}

static double kaiser7_fft_window(int i,int len){
	return kaiser_fft_window(7.f, i, len);
}