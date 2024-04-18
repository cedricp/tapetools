#include <cmath>               // for fabs
#include <string.h>

float mean(const float data[], int len) {
    float sum = 0.0, mean = 0.0;

    int i;
    for(i=0; i<len; ++i) {
        sum += data[i];
    }

    mean = sum/len;
    return mean;


}

float stddev(const float data[], int len) {
    float the_mean = mean(data, len);
    float standardDeviation = 0.0;

    int i;
    for(i=0; i<len; ++i) {
        standardDeviation += powf(data[i] - the_mean, 2.f);
    }

    return sqrt(standardDeviation/len);
}

void smoothed_z_score(const float y[], float signals[], const int count, const int lag, const float threshold, const float influence)
{
    memset(signals, 0, sizeof(float) * count);
    float filteredY[count];
    memcpy(filteredY, y, sizeof(float) * count);
    float avgFilter[count];
    float stdFilter[count];

    avgFilter[lag - 1] = mean(y, lag);
    stdFilter[lag - 1] = stddev(y, lag);

    for (int i = lag; i < count; i++) {
        if (fabsf(y[i] - avgFilter[i-1]) > threshold * stdFilter[i-1]) {
            if (y[i] > avgFilter[i-1]) {
                signals[i] = 1;
            } else {
                signals[i] = -1;
            }
            filteredY[i] = influence * y[i] + (1 - influence) * filteredY[i-1];
        } else {
            signals[i] = -0;
        }
        avgFilter[i] = mean(filteredY + i-lag, lag);
        stdFilter[i] = stddev(filteredY + i-lag, lag);
    }
}


