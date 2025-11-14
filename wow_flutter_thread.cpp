#include "wow_flutter_thread.h"
#include <array>

extern const int WOW_FLUTTER_DECIMATION;
const std::array<int, 4> filter_mapping = {0, 6, 20, 100};

WowAndFluterThread::WowAndFluterThread(AudioToolWindow& mainwin, int ref_frequency, int samplerate) : ASyncTask("WFtask"),
    m_longterm_audio(mainwin.m_longterm_audio), m_wow_flutter_data(mainwin.m_wow_flutter_data),
    m_wow_flutter_data_x(mainwin.m_wow_flutter_data_x), m_wow_peak(mainwin.m_wow_peak_detection),
    m_samplerate(samplerate), m_analysis_time_s(WOW_FLUTTER_ANALYSIS_TIME), m_decimation(WOW_FLUTTER_DECIMATION),
    m_reference_frequency(ref_frequency), m_mutex(mainwin.m_wow_data_mutex), m_wow_mean(mainwin.m_wow_mean),
    m_wowfftplan(mainwin.m_fftplanwow), m_wow_fftdrawout(mainwin.m_fftdrawwow), m_wow_complex_fftout(mainwin.m_wow_complex_out),
    m_wow_fftwowdrawfreqs(mainwin.m_fftwowdrawfreqs), m_signal_i(mainwin.m_signal_i), m_signal_q(mainwin.m_signal_q), m_time(mainwin.m_wf_compute_time),
    m_compute_fft(mainwin.m_show_wf_fft_view)
{
    m_filter_freq = mainwin.m_wf_filter_freq_combo < filter_mapping.size() ? filter_mapping[mainwin.m_wf_filter_freq_combo] : 0;
}

WowAndFluterThread::~WowAndFluterThread()
{
}


// Main thread WF code
void WowAndFluterThread::entry()
{
    Chrono chrono;
    // Init low pass filter
    m_iq_lowpass_filter.setup(4, m_samplerate, 700, 0.1);
    m_wf_lowpass_filter.setup(4, m_samplerate / m_decimation, m_filter_freq, 0.1);

    // We need ~5 seconds of audio recording
    {
        ScopedMutex mutex(m_mutex);

        int actual_audio_length = m_longterm_audio.size();
        double current_samplerate = m_samplerate;
        double inv_current_samplerate = 1. / m_samplerate;
        double twopi_over_sr = 2. * M_PI / m_samplerate;
        double reference_freq_times_twopi_over_sr = m_reference_frequency * twopi_over_sr;

        m_signal_i.resize(actual_audio_length);
        m_signal_q.resize(actual_audio_length);
        
        // real signal to IQ data
        for (int i = 0; i < actual_audio_length; ++i)
        {
            // Convert audio to Inphase (cos)/Quadrature(sine) data
            m_signal_i[i] = m_longterm_audio[i] * cos(reference_freq_times_twopi_over_sr * double(i));
            m_signal_q[i] = m_longterm_audio[i] * sin(reference_freq_times_twopi_over_sr * double(i));
        }

        // Low pass filter IQ signal to suppress fundamental
        double *lp_chans[2] = {m_signal_i.data(), m_signal_q.data()};
        m_iq_lowpass_filter.process(actual_audio_length, lp_chans);

        int decimated_size = m_wow_flutter_data.size();
        int decimated_samplerate = m_samplerate / WOW_FLUTTER_DECIMATION;
        double phase_to_hz = (m_samplerate / (M_PI * 2.));

        for (int i = 1; i < decimated_size; i++)
        {
            int step_i = i * m_decimation;

            // Start graph a little later to hide LPF settle time
            int step_i_x = (i - (decimated_size / 10)) * m_decimation;
            double phase0 = complex_argument(m_signal_q[step_i-1], m_signal_i[step_i-1]);
            double phase1 = complex_argument(m_signal_q[step_i], m_signal_i[step_i]);
            double phase_diff = wrap_phase(phase0 - phase1);

            // Convert phase difference to Hertz
            m_wow_flutter_data[i] = phase_diff * phase_to_hz;
            m_wow_flutter_data_x[i] = (double)step_i_x * inv_current_samplerate;
            if (i >= m_wow_flutter_data.size()){
                continue;
            }
        }

        // Process low pass filtering of W&F data
        if(m_filter_freq > 0)
        {
            m_wf_lowpass_filter.reset();
            lp_chans[0] = m_wow_flutter_data.data();
            m_wf_lowpass_filter.process(decimated_size, lp_chans);
            //m_wf_lowpass_filter.processInterleaved(decimated_size, lp_chans);
        }

        double max_dev = -1000, min_dev = 1000, mean = 0;
        int num_samples = 0;

        // I start the measure a little after the beginning to suppress lpf settling part
        for (int i = decimated_size/10; i < decimated_size; ++i)
        {
            double current = m_wow_flutter_data[i];
            if (current > max_dev) max_dev = current;
            if (current < min_dev) min_dev = current;
            mean += current;
            num_samples++;
        }

        mean /= num_samples;
        double peak_plus = fabs(max_dev - mean);
        double peak_minus = fabs(mean - min_dev);
        m_wow_peak = peak_plus > peak_minus ? peak_plus : peak_minus;
        m_wow_mean = mean;

        // Process FFT compute of the W&F data
        if (m_compute_fft)
        {
            fftw_execute(m_wowfftplan);

            const int fftdraw_size = (decimated_samplerate * (WOW_FLUTTER_ANALYSIS_TIME - 0.5f)) / 2;
            const double inv_fft_capture_size = 1./fftdraw_size;
            const double fft_step = (decimated_samplerate / 2.) * inv_fft_capture_size;

            for(int i = 0; i < fftdraw_size; ++i)
            {
                double fftout = complex_module(m_wow_complex_fftout[i][FFTW_IMAGINARY_INDEX], m_wow_complex_fftout[i][FFTW_REAL_INDEX]) * inv_fft_capture_size;
                m_wow_fftdrawout[i] = fftout;
                m_wow_fftwowdrawfreqs[i] = fft_step * i;
            }

            // Normalize DC component
            m_wow_fftdrawout[0] *= 0.5;
        }
    }
    m_time = chrono.get_elapsed_time();
}