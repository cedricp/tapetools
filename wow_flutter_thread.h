#pragma once

#include <vector>
#include <fftw3.h>
#include <utils.h>
#include <Dsp.h>
#include <thread.h>
#include "main_widget.h"

class WowAndFluterThread : public ASyncTask
{
    // Data
    const std::vector<double> &m_longterm_audio;
    std::vector<double> &m_wow_flutter_data;
    std::vector<double> &m_wow_flutter_data_x;
    std::vector<double> &m_signal_i;
    std::vector<double> &m_signal_q;
    std::vector<fftw_complex> m_signal_iq;
    float &m_wow_peak;
    float &m_wow_mean;
    int m_samplerate;
    double m_reference_frequency;
    float m_analysis_time_s;
    float m_filter_freq;
    int   m_decimation;
    const bool &m_compute_fft;

    const fftw_plan& m_wowfftplan;
    std::vector<double>& m_wow_fftdrawout;
    std::vector<double>& m_wow_fftwowdrawfreqs;
    fftw_complex* m_wow_complex_fftout;
    unsigned long &m_time;

    // Objects
    Dsp::SimpleFilter <Dsp::ChebyshevI::LowPass <4>, 2> m_iq_lowpass_filter;
    Dsp::SimpleFilter <Dsp::ChebyshevI::LowPass <4>, 1> m_wf_lowpass_filter;
    Dsp::SimpleFilter <Dsp::ChebyshevI::BandPass <4>, 1> m_wf_lowpass_prefilter;
    ThreadMutex& m_mutex;
public:
    WowAndFluterThread(AudioToolWindow& mainwin, int ref_frequency, int samplerate);
    ~WowAndFluterThread();

private:
    void entry() override;
};