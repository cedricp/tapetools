#include <vector>
#include <fftw3.h>
#include <utils.h>
#include <Dsp.h>
#include <thread.h>

extern const int WOW_FLUTTER_DECIMATION;

class WowAndFluterThread : public ASyncTask
{
    // Data
    std::vector<double> &m_longterm_audio;
    std::vector<double> &m_wow_flutter_data;
    std::vector<double> &m_wow_flutter_data_x;
    std::vector<double> m_signal_i;
    std::vector<double> m_signal_q;
    float &m_wow_peak;
    float &m_wow_mean;
    int m_samplerate;
    double m_reference_frequency;
    float m_analysis_time_s;
    float m_filter_freq;
    int   m_decimation;

    fftw_plan& m_wowfftplan;
    std::vector<double>& m_wow_fftdrawout;
    std::vector<double>& m_wow_fftfreqs;
    fftw_complex* m_wow_fftout;

    // Objects
    Dsp::SimpleFilter <Dsp::ChebyshevI::LowPass <4>, 2> m_iq_lowpass_filter;
    Dsp::SimpleFilter <Dsp::ChebyshevI::LowPass <4>, 1> m_wf_lowpass_filter;
    ThreadMutex& m_mutex;
public:
    WowAndFluterThread(int sr, std::vector<double>& longtermaudio, std::vector<double> &wow_flutter_data,
        std::vector<double> &wow_flutter_data_x, int frequency, ThreadMutex& mutex,
        float analysis_time_s, int filter_freq, float& wow_peak, float& wow_mean, int decimation, std::vector<double>& wow_fftdrawout,
        fftw_complex* wow_fftout, std::vector<double>& wow_fftfreqs, fftw_plan& fft_plan)
         : m_longterm_audio(longtermaudio), m_samplerate(sr), m_analysis_time_s(analysis_time_s),
        m_wow_flutter_data(wow_flutter_data), m_wow_flutter_data_x(wow_flutter_data_x),
        m_reference_frequency(frequency), m_mutex(mutex), m_wow_peak(wow_peak),
        m_wow_mean(wow_mean), m_decimation(decimation), m_wowfftplan(fft_plan), m_wow_fftdrawout(wow_fftdrawout),
        m_wow_fftout(wow_fftout), m_wow_fftfreqs(wow_fftfreqs), ASyncTask("WFtask")
    {
        m_filter_freq = 0;
        if (filter_freq == 1) m_filter_freq = 6;
        if (filter_freq == 2) m_filter_freq = 20;
        if (filter_freq == 3) m_filter_freq = 100;
    }

    ~WowAndFluterThread()
    {
        //m_chrono.print_elapsed_time("WF thread time : ");
    }

private:
    void entry() override
    {
        // Init low pass filter
        m_iq_lowpass_filter.setup(4, m_samplerate, 700, 0.1);

        // We need ~5 seconds of audio recording
        m_mutex.lock();
            int actual_audio_length = m_longterm_audio.size();
            double current_samplerate = m_samplerate;
            double inv_current_samplerate = 1. / m_samplerate;
            double twopif_over_sr = 2. * M_PI / m_samplerate;

            m_signal_i.resize(actual_audio_length);
            m_signal_q.resize(actual_audio_length);
            
            // real signal to IQ data
            for (int i = 0; i < actual_audio_length; ++i)
            {
                m_signal_i[i] = m_longterm_audio[i] * cos(m_reference_frequency*double(i) * twopif_over_sr);
                m_signal_q[i] = m_longterm_audio[i] * sin(m_reference_frequency*double(i) * twopif_over_sr);
            }
        m_mutex.unlock();

        // Low pass filter IQ signal to suppress fundamental
        double *lp_chans[2] = {m_signal_i.data(), m_signal_q.data()};
        m_iq_lowpass_filter.process(actual_audio_length, lp_chans);

        int decimated_size = actual_audio_length / m_decimation;
        int decimated_samplerate = m_samplerate / WOW_FLUTTER_DECIMATION;
        double phase_to_hz = (m_samplerate / (M_PI * 2.));

        m_mutex.lock();  
            for (int i = 1; i < decimated_size; i++)
            {
                int step_i = i * m_decimation;
                // Start graph a little later to hide LPF settle time
                int step_i_x = (i - (decimated_size / 10)) * m_decimation;
                double phase_diff = wrap_phase(atan2(m_signal_q[step_i-1], m_signal_i[step_i-1]) - atan2(m_signal_q[step_i], m_signal_i[step_i]));
                // Convert phase difference to Hertz
                m_wow_flutter_data[i] = phase_diff * phase_to_hz;
                m_wow_flutter_data_x[i] = (double)step_i_x * inv_current_samplerate;
            }

            if(m_filter_freq > 0)
            {
                m_wf_lowpass_filter.setup(4, m_samplerate / m_decimation, m_filter_freq, 0.1);
                lp_chans[0] = m_wow_flutter_data.data();
                m_wf_lowpass_filter.process(decimated_size, lp_chans);
            }

            double max_dev = -1000, min_dev = 1000, mean = 0;
            int num_samples = 0;
            // I start the measure a little after the beginnig to suppress lpf settling part
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

            fftw_execute(m_wowfftplan);

            const int fftdraw_size = (decimated_samplerate * (WOW_FLUTTER_ANALYSIS_TIME - 0.5f)) / 2.;
            const double inv_fft_capture_size = 1./fftdraw_size;
            const double fft_step = (decimated_samplerate / 2.) * inv_fft_capture_size;

            for(int i = 0; i < fftdraw_size; ++i)
            {
                double fftout = sqrt(m_wow_fftout[i][0] * m_wow_fftout[i][0] + m_wow_fftout[i][1] * m_wow_fftout[i][1]) * inv_fft_capture_size;
                m_wow_fftdrawout[i] = fftout;
                m_wow_fftfreqs[i] = fft_step * i;
            }

            // Normalize DC component
            m_wow_fftdrawout[0] *= 0.5;
        m_mutex.unlock();
    }
};