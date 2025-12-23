#pragma once

#include <math.h>


class SineWave {
    int m_function = 0;
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
    enum WaveformFunction {
        SINE_WAVE,
        SQUARE_WAVE,
        TRIANGLE_WAVE,
    };

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

    void set_function(WaveformFunction s){
        m_function = s;
    }

    double sample() {
        double sample = 0;
        if (m_function == SQUARE_WAVE)
        {
            sample = fmod(phase, M_PI * 2) < M_PI ? amplitude : -amplitude;
        }
        else if (m_function == TRIANGLE_WAVE)
        {
            double value = fmod(phase, M_PI * 2) / (M_PI * 2);
            if (value < 0.25)
                sample = (value * 4);
            else if (value < 0.75)
                sample = (2.0 - (value * 4));
            else
                sample =  ((value * 4) - 4.0);
        }
        else if (m_function == SINE_WAVE)
        {
            sample = sin(phase);
        }
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
        return sample * amplitude;
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