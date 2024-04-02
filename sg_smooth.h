#ifndef __SGSMOOTH_HPP__
#define __SGSMOOTH_HPP__

#include <vector>

// savitzky golay smoothing.
bool sg_smooth(const float *v, float *res, const int size, const int width, const int deg);
//! numerical derivative based on savitzky golay smoothing.
std::vector<float> sg_derivative(const std::vector<float> &v, const int w,
                                const int deg, const float h=1.0);
void smoothed_z_score(const float y[], float signals[], const int count, const int lag, const float threshold, const float influence);
#endif // __SGSMOOTH_HPP__