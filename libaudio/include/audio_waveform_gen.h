#pragma once

#include "audio_manager.h"
#include <math.h>

struct Transition {
    double
        amplitude,
        amplitudeStep,
        frequency,
        frequencyStep;
};

class SineWave {
    Transition transition;
public:
    double
        amplitude,
        rate,           // sample rate
        frequency,      // oscillation Hz.
        phase,          // current phase
        phaseStep;      // phase step

    const double radians = (M_PI * 2);
    bool double_equals(double a, double b)
    {
        return abs(a - b) < 0.001;
    }

    SineWave(double freq, double rate, double amp){
        set(freq, rate, amp);
    }

    SineWave(){

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


class audioWaveformGenerator{
public:
    enum generatorMode{
        SINE,
        WHITE_NOISE,
        BROWN_NOISE
    };
private:
    double m_seconds_offset = 0;
    double m_pitch;
    double m_volume = 1.0;
    double m_fm_freq = 0.0;
    double m_fm_strength = 1.0;
    int m_mode = SINE;
    SoundIoOutStream *m_outstream = nullptr;
    SineWave m_sinewave;

    static void write_callback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max);
    static void underflow_callback(SoundIoOutStream *outstream);
    static void error_callback(SoundIoOutStream *outstream, int err);
public:
    audioWaveformGenerator();
    ~audioWaveformGenerator();

    void destroy();
    bool init(audioManager& manager, int device_idx, int samplerate, float latency);
    bool start();
    bool pause(bool pause = true);

    int get_samplerate();

    void set_pitch(double pitch);
    void set_volume(int db);
    void set_fm(double pitch,double vol){m_fm_freq = pitch; m_fm_strength = vol;}

    void set_mode(int m){m_mode = m;}
    int& mode(){return m_mode;};
};