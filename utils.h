#pragma once

#include <vector>

bool sg_smooth(const float *v, float *res, const int size, const int width, const int deg);
std::vector<float> sg_derivative(const std::vector<float> &v, const int w,
                                const int deg, const float h=1.0);
void smoothed_z_score(const float y[], float signals[], const int count, const int lag, const float threshold, const float influence);

float rectangle_fft_window(int i, int length)
{
	return 1.0;
}

float hamming_fft_window(int i, int length)
{
	float a, b, w, N1;
	a = 25.0/46.0;
	b = 21.0/46.0;
	N1 = (float)(length-1);
	w = a - b*cos(2*i*M_PI/N1);
	return w;
}

float hann_fft_window(int i, int length)
{
    return 0.5f*(1.0f-cosf(2.0f*M_PI*(float)(i)/(float)(length-1.0f)));
}

float hann_poisson_fft_window(int i, int length)
{
	float a, N1, w;
	a = 2.0;
	N1 = (float)(length-1);
	w = 0.5 * (1 - cosf(2*M_PI*i/N1)) * \
	    pow(M_E, (-a*(float)abs((int)(N1-1-2*i)))/N1);
	return w;
}

float blackman_fft_window(int i, int length)
{
	float a0, a1, a2, w, N1;
	a0 = 7938.0/18608.0;
	a1 = 9240.0/18608.0;
	a2 = 1430.0/18608.0;
	N1 = (float)(length-1);
	w = a0 - a1*cosf(2*i*M_PI/N1) + a2*cosf(4*i*M_PI/N1);
	return w;
}

float blackman_harris_fft_window(int i, int length)
{
	float a0, a1, a2, a3, w, N1;
	a0 = 0.35875;
	a1 = 0.48829;
	a2 = 0.14128;
	a3 = 0.01168;
	N1 = (float)(length-1);
	w = a0 - a1*cosf(2*i*M_PI/N1) + a2*cosf(4*i*M_PI/N1) - a3*cosf(6*i*M_PI/N1);
	return w;
}

static float zeroethOrderBessel(float x)
{
	const float eps = 0.000001;

	//  initialize the series term for m=0 and the result
	float besselValue = 0;
	float term = 1;
	float m = 0;

	//  accumulate terms as long as they are significant
	while(term  > eps * besselValue){
		besselValue += term;
			
		//  update the term
		++m;
		term *= (x*x) / (4*m*m);
	}
	return besselValue;
}

static float keiser5_fft_window(int i,int len){   
     //  Pre-compute the shared denominator in the Kaiser equation. 
	const float shape = 5.f;
	const float oneOverDenom = 1.0 / zeroethOrderBessel( shape );
	const unsigned int N = len - 1;
	const float oneOverN = 1.0 / N;
	const float K = (2.0 * (float)(i) * oneOverN) - 1.0;
	const float arg = sqrtf( 1.0 - (K * K) );        
	return zeroethOrderBessel(shape * arg) * oneOverDenom;
}