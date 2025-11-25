#pragma once

#include <math.h>


class SineWave {
    struct Transition {
    double
        amplitude,
        amplitudeStep,
        frequency,
        frequencyStep;
    } transition;
    
    double
        amplitude,
        rate,           // sample rate
        frequency,      // oscillation Hz.
        phase,          // current phase
        phaseStep;      // phase step
public:

    const double radians = (M_PI * 2);
    bool double_equals(double a, double b)
    {
        return abs(a - b) < 0.001;
    }

    SineWave(double freq, double rate, double amp){
        set(freq, rate, amp);
    }

    SineWave(){
        set(440.0, 44100.0, 1.0);
    }

    void set(double freq, double rate, double amp){
        amplitude = amp;
        frequency = freq;
        this->rate = rate;
        amplitude = amp;
        transition.amplitude = amp;
        transition.amplitudeStep = 0;
        transition.frequency = freq;
        transition.frequencyStep = 0;
        phase = 0.0;
        phaseStep = 0;
    }

    double sine_wave_sample() {
        double sample = amplitude * sin(phase);
        phase += phaseStep;
        if (transition.frequencyStep) {
            frequency += transition.frequencyStep;
            phaseStep = radians * frequency / rate;
            if (double_equals(frequency, transition.frequency)){
                transition.frequencyStep = 0;
                frequency = transition.frequency;
            }
        }
        if (transition.amplitudeStep) {
            amplitude += transition.amplitudeStep;
            if (double_equals(amplitude, transition.amplitude)) {
                transition.amplitudeStep = 0;
                amplitude = transition.amplitude;
            }
        }
        return sample;
    }

    void sine_wave_frequency_transition(double freq, double duration) {
        transition.frequency = freq;
        transition.frequencyStep = (freq - frequency) / (rate * duration);
    }
    
    void sine_wave_amplitude_transition(double ampl, double duration) {
        transition.amplitude = ampl;
        transition.amplitudeStep = (ampl - amplitude) / (rate * duration);
    }
    
};