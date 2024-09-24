#define _USE_MATH_DEFINES

#include "SDL.h"
#include "window_sdl.h"
#include "audio_sine_gen.h"
#include "audio_record.h"
#include "utils.h"
#include "timer.h"
#include "lcd_display.h"
#include <fftw3.h>
#include <complex>
#include <thread.h>
#include <algorithm>
#include <stdarg.h>
#include <Dsp.h>
#include <scanner.h>
#include "Hack-Regular.h"

const double WOW_FLUTTER_ANALYSIS_TIME = 5.5;
const int    WOW_FLUTTER_DECIMATION = 20;

void TextCenter(const char* text, ...) {
    char buffer[256];
    va_list args;
    va_start(args, text);
    vsnprintf(buffer, 256, text, args);
    va_end(args);


    float font_size = ImGui::GetFontSize() * strlen(buffer) / 2;
    ImGui::SetCursorPosX(ImGui::GetWindowSize().x / 2 - font_size + (font_size / 2));

    ImGui::Text("%s", buffer);
}

class SdrThread : public Thread
{
    SDR_Scanner m_scanner;
    bool m_data_available = false;
public:
    SdrThread() : Thread("SdrThread", true, false)
    {
        m_scanner.init();
    }

    ~SdrThread()
    {
        stop();
    }

    void entry() override
    {
        if (m_scanner.scan() == SCANNER_NOK)
        {
            usleep(500000);
            m_scanner.init();
        }
        m_data_available = true;
    }

    bool data_available()
    {
        return m_data_available;
    }

    const std::vector<SDR_Scanner::Scan_result>& get_scan_result()
    {
        m_data_available = false;
        return m_scanner.get_scan_result();
    }
};

class WowAndFluterThread : public ASyncTask
{
    // Data
    std::vector<double> &m_longterm_audio;
    std::vector<double> &m_wow_flutter_data;
    std::vector<double> &m_wow_flutter_data_x;
    std::vector<double> m_incomimg_sound_data;
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
    double* m_wow_fftdrawout;
    double* m_wow_fftfreqs;
    fftw_complex* m_wow_fftout;

    // Objects
    Dsp::SimpleFilter <Dsp::ChebyshevI::LowPass <4>, 2> m_iq_lowpass_filter;
    Dsp::SimpleFilter <Dsp::ChebyshevI::LowPass <4>, 1> m_wf_lowpass_filter;
    ThreadMutex& m_mutex;
public:
    WowAndFluterThread(int sr, std::vector<double>& longtermaudio, std::vector<double> &wow_flutter_data,
        std::vector<double> &wow_flutter_data_x, std::vector<double> incoming_data, int frequency, ThreadMutex& mutex,
        float analysis_time_s, int filter_freq, float& wow_peak, float& wow_mean, int decimation, double* wow_fftdrawout,
        fftw_complex* wow_fftout, double* wow_fftfreqs, fftw_plan& fft_plan)
         : m_longterm_audio(longtermaudio), m_samplerate(sr), m_analysis_time_s(analysis_time_s),
        m_wow_flutter_data(wow_flutter_data), m_wow_flutter_data_x(wow_flutter_data_x),
        m_incomimg_sound_data(incoming_data), m_reference_frequency(frequency), m_mutex(mutex), m_wow_peak(wow_peak),
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
            double inv_current_samplerate = 1. / current_samplerate;
            double twopif_over_sr = 2. * M_PI / current_samplerate;

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
        double phase_to_hz = (current_samplerate / (M_PI * 2.));

        m_mutex.lock();  
            for (int i = 1; i < decimated_size; i++)
            {
                int step_i = i * m_decimation;
                // Start graph a little later to hide LPF settle time
                int step_i_x = (i - (decimated_size / 10)) * m_decimation;
                double phase = wrap_phase(atan2(m_signal_q[step_i-1], m_signal_i[step_i-1]) - atan2(m_signal_q[step_i], m_signal_i[step_i]));
                m_wow_flutter_data[i] = phase * phase_to_hz;
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

            const int fftdraw_size = decimated_size / 2;
            const double inv_fft_capture_size = 1./fftdraw_size;
            const double fft_step = (m_samplerate / WOW_FLUTTER_DECIMATION / 2.) * inv_fft_capture_size;

            for(int i = 0; i < fftdraw_size; ++i)
            {
                double fftout = sqrt(m_wow_fftout[i][0] * m_wow_fftout[i][0] + m_wow_fftout[i][1] * m_wow_fftout[i][1]) * inv_fft_capture_size;
                m_wow_fftdrawout[i] = fftout;
                m_wow_fftfreqs[i] = fft_step * i;
            }
        m_mutex.unlock();
    }
};

class AudioToolWindow : public Widget
{
    audioManager m_audiomanager;
    audioSineGenerator m_sine_generator;
    audioRecorder m_audiorecorder;

    int  m_uitheme = 0;
    
    bool m_sine_generator_switch = false;
    int  m_pitch = 1000;
    float m_sinegen_latency_s = 0.1f;
    int m_recorder_latency_ms = 100;
    int m_sine_volume_db = 0.f;
    
    int m_audio_out_idx = -1;
    int m_audio_in_idx = -1;

    std::string m_input_device;
    std::string m_output_device;
    
    std::vector<double> m_sound_data1, m_sound_data2;
    std::vector<double> m_longterm_audio;
    std::vector<double> m_wow_flutter_data, m_wow_flutter_data_x;
    std::vector<double> m_sound_data_x;
    std::vector<double> m_raw_buffer;
    fftw_plan m_fftplanr = NULL;
    fftw_plan m_fftplanl = NULL;
    fftw_plan m_fftplanwow = NULL;
    double *m_fftinl = nullptr;
    fftw_complex *m_fftoutl = nullptr;
    double *m_fftinr = nullptr;
    fftw_complex *m_fftoutr = nullptr;
    fftw_complex *m_fftoutwow = nullptr;
    double *m_rms_fft = nullptr;
    double *m_fftdrawl = nullptr;
    double *m_fftdrawr = nullptr;
    double *m_fftdrawwow = nullptr;
    double *m_fftfreqs = nullptr;
    double *m_fftwowdrawfreqs = nullptr;
    int m_capture_size = 0;
    double m_audio_gain = 1.0f;
    int m_combo_in = 0;
    int m_combo_out = 0;
    int m_in_sample_rate_idx = 0;
    int m_out_sample_rate_idx = 0;
    int m_wow_flutter_capture_size = 0;

    bool m_sound_setup_open = false;
    bool m_compute_channel_phase = false;
    bool m_logscale_frequency = true;
    bool m_show_wow_flutter = false;
    bool m_show0db = false;
    double m_rms_calibration_scale = 1.0f;
    float m_scopezoom = 1;;
    std::vector<std::string> m_wmodes = {"Rectangle", "Hamming", "Hann-Poisson", "Blackman", "Blackman-Harris", "Hann", "Kaiser 5", "Kaiser 7"};
    std::vector<std::string> m_fftchannels = {"Left", "Right"};
    double m_window_amplitude_correction[8] = {0.0};
    double m_window_energy_correction[8] = {0.0};

    double  (*m_window_fn)(int, int) = hann_fft_window;
    int     m_fft_window_fn_index = 5;
    double  *m_current_window_cache = nullptr;
    bool    m_fft_channel_left = true;
    bool    m_fft_channel_right = false;
    double  m_noise_foor = -100;
    double  m_fft_highest_pos[20];
    int     m_fft_highest_idx[20];
    double  m_fft_highest_val;
    int     m_fft_found_peaks = 0;
    int     m_fundamental_index = 0;
    int     m_fft_fund_idx_range_min = 0;
    int     m_fft_fund_idx_range_max = 0;
    double  m_thd = 0;
    double  m_thdn = 0;
    double  m_thddb = 0;
    double  m_fft_rms = 0;
    bool    m_show_thd = false;

    double  m_left_right_db;
    double  m_phase_diff_degrees;
    std::vector<float> m_phase_history;
    std::vector<float> m_lrdiff_history;
    std::vector<float> m_phase_time;

    double  m_rms_left = 0, m_rms_right = 0;
    bool    m_show_rms_voltage = false;
    double  m_frequency_counter = 0;

    bool    m_sweep_started = false;
    bool    m_async_sweep = false;
    int     m_sweep_current_frequency;
    int     m_sweep_capture_num = 30;
    int     m_measure_delay = 400;
    int     m_sweep_last_measure_freq;
    float   m_sweep_threshold_level = -50;
    std::vector<double> m_sweep_values;
    std::vector<double> m_sweep_freqs;
    Timer   m_sweep_timer;
    bool    m_compute_on = false;

    bool    m_use_targetdb = false;
    bool    m_lockdb = false;
    float   m_target_db = 0.0;
    double  m_locked_db_value = 0.0;
    int     m_current_db_target_channel = 0;

    int     m_zscore_lag = 17;
    float   m_zscore_influence = 0.5;
    float   m_zscore_threshold = 3.0;
    bool    m_show_zscore_settings = false;
    bool    m_optimized_fft = false;

    int     m_wow_test_frequency = 1;
    int     m_wow_test_frequency_custom = 3000;
    float   m_wow_peak_detection = 0;
    int     m_wf_filter_freq_combo = 0;
    float   m_wow_mean = 0;
    ThreadMutex m_wow_data_mutex;

    SdrThread m_sdr_thread;

    CALLBACK_METHOD(on_timer_event, AudioToolWindow)
    {
        
        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        float fft_step = m_capture_size / current_sample_rate;
        double* current_fft_draw = m_fft_channel_left ? m_fftdrawl : m_fftdrawr;

        if (!m_async_sweep)
        {
            int min_freq_idx = std::max(int((m_sweep_current_frequency-50)*fft_step), 0);
            int max_freq_idx = std::min(int((m_sweep_current_frequency+50)*fft_step), m_capture_size / 2);

            double max_val = m_noise_foor;
            for (int i = min_freq_idx; i < max_freq_idx; ++i)
            {
                if (current_fft_draw[i] > max_val) max_val = current_fft_draw[i];
            }

            m_sweep_values.push_back(max_val);
            m_sweep_freqs.push_back(m_sweep_current_frequency);

            double logfreq_min = log10(20.);
            double logfreq_max = log10(20000.);
            double step = (logfreq_max-logfreq_min) / m_sweep_capture_num;

            double newlogfreq = log10(m_sweep_current_frequency) + step;

            m_sweep_current_frequency = pow(10., newlogfreq);
            m_sine_generator.set_pitch(m_sweep_current_frequency);

            if (m_sweep_current_frequency >= 20000)
            {   
                // We reached the end of the measure
                stop_sweep_gen();
                return;
            }
        } else {
            double fft_max_val = m_sweep_threshold_level;
            double frequency = -1;
            for (int i = 1; i < m_capture_size / 2; ++i)
            {
                if (current_fft_draw[i] > fft_max_val)
                {
                    // TODO : Check selected channel 
                    fft_max_val = current_fft_draw[i];
                    frequency = double(i) / fft_step;
                }
            }
            if (frequency < 0)
            {
                return;
            }
            int sweep_values_index = 0;
            bool found_bin = false;
            for (auto freq : m_sweep_freqs)
            {
                double freq_low = freq-(freq*0.1);
                double freq_hi  = freq+(freq*0.1);
                if (frequency > freq_low && frequency < freq_hi)
                {
                    if (m_sweep_values[sweep_values_index] < fft_max_val)
                    m_sweep_values[sweep_values_index] = fft_max_val;
                    found_bin = true;
                    break;
                }
                if (freq > frequency && sweep_values_index >= 0)
                {
                    m_sweep_values.insert(m_sweep_values.begin() + sweep_values_index, fft_max_val);
                    m_sweep_freqs.insert(m_sweep_freqs.begin() + sweep_values_index, frequency);
                    found_bin = true;
                    break;
                }
                sweep_values_index++;
            }
            if (!found_bin)
            {
                m_sweep_values.push_back(fft_max_val);
                m_sweep_freqs.push_back(frequency);
            }
            m_sweep_last_measure_freq = frequency > m_sweep_last_measure_freq ? frequency : m_sweep_last_measure_freq;
        }
        
        m_sweep_timer.start();
        update_ui();
    }

    CALLBACK_METHOD(on_device_changed, AudioToolWindow)
    {
        printf("Audio device configuration changed.\n");
    }

    CALLBACK_METHOD(on_backend_disconnected, AudioToolWindow)
    {
        reset_audiomanager();
    }

    void reset_audiomanager()
    {
        if (m_input_device.empty()) m_audio_in_idx  = m_audiomanager.get_default_input_device_id();
        if (m_output_device.empty()) m_audio_out_idx = m_audiomanager.get_default_output_device_id();

        m_combo_in  = m_audiomanager.get_input_device_reverse_map(m_audio_in_idx);
        m_combo_out = m_audiomanager.get_output_device_reverse_map(m_audio_out_idx);
        reinit_recorder();
        reset_sine_generator();
    }

    void detect_periods(){
        double timestep = 1. / (double)m_audiorecorder.get_current_samplerate();
        double *audio_data = m_sound_data1.data();
        std::vector<double> frequencies;

        int previous_idx = 0;
        double lastzerocross = 0;
        double previous = audio_data[0];
        double previous_time = 0;
        double freq_mean = 0;

        for (int i = 1; i < m_capture_size; ++i)
        {
            double current = audio_data[i];
            
            if (previous < 0 && current > 0)
            {
                double a[2] = {timestep * ((double)i-1.), previous};
                double b[2] = {timestep * (double)i, current};
                double zcrosstime = zerocross(a,b);
                if (lastzerocross > 0)
                {
                    double freq = 1.0 / (zcrosstime - lastzerocross);
                    frequencies.push_back(freq);
                    freq_mean += freq;
                }
                lastzerocross = zcrosstime;
            }
            previous = current;
        }

        if (frequencies.size() < 2)
        {
            m_frequency_counter = 0;
            return;
        }

        freq_mean /= (double)frequencies.size();
        m_frequency_counter = freq_mean;
        double last = frequencies[0];
        double maxdeviation = 0;
        for (int i = 1; i < frequencies.size();++i){
            double diff = fabs(last - frequencies[i]);
            if (diff > maxdeviation) maxdeviation = diff;
        }
    }

public:
    AudioToolWindow(Window_SDL* win) : Widget(win, "AudioTools"), m_audiorecorder(m_audiomanager), m_sweep_timer(m_measure_delay, true)
    {
        set_maximized(true);
        set_movable(false);
        set_resizable(false);
        set_titlebar(false);

        compute_fft_window_corrections();
        reset_audiomanager();
        set_theme();

        m_audiomanager.device_changed_event.connect_event(STATIC_METHOD(on_device_changed), this);
        m_audiomanager.backend_disconnected_event.connect_event(STATIC_METHOD(on_backend_disconnected), this);

        m_audiomanager.flush();
        m_sweep_timer.connect_event(STATIC_METHOD(on_timer_event), this);

        //m_sdr_thread.start();
    }

    virtual ~AudioToolWindow()
    {
        m_sine_generator.destroy();
        destroy_capture();
        while(App_SDL::get()->get_thread("WFtask"))
        {
            App_SDL::get()->release_finished_threads();
        }
        m_sdr_thread.stop();
    }

    void destroy_capture()
    {
        if (m_fftplanr) fftw_destroy_plan(m_fftplanr);
        if (m_fftplanl) fftw_destroy_plan(m_fftplanl);
        if (m_fftplanwow) fftw_destroy_plan(m_fftplanwow);

        delete[] m_fftinl;
        delete[] m_fftoutl;
        delete[] m_fftinr;
        delete[] m_fftoutr;
        delete[] m_fftdrawl;
        delete[] m_fftdrawr;
        delete[] m_fftfreqs;
        delete[] m_fftwowdrawfreqs;
        delete[] m_fftoutwow;
        delete[] m_rms_fft;
        delete[] m_current_window_cache;
        delete[] m_fftdrawwow;
        m_sound_data_x.clear();

        m_fftinl = nullptr;
        m_fftoutl = nullptr;
        m_fftinr = nullptr;
        m_fftoutr = nullptr;
        m_fftdrawl = nullptr;
        m_fftdrawr = nullptr;
        m_fftfreqs = nullptr;
        m_fftwowdrawfreqs = nullptr;
        m_fftplanr = nullptr;
        m_fftplanl = nullptr;
        m_fftoutwow = nullptr;
        m_fftdrawwow = nullptr;
        m_current_window_cache = nullptr;
        m_rms_fft = nullptr;
        m_fftplanwow = nullptr;
    }

    void init_capture()
    {
        int capture_size = m_audiorecorder.get_buffer_size(float(m_recorder_latency_ms) / 1000.f, false);
        int samplerate = m_audiorecorder.get_current_samplerate();
        if (capture_size == 0) return;
        
        destroy_capture();
        m_capture_size = capture_size;
        m_wow_flutter_capture_size = samplerate / WOW_FLUTTER_DECIMATION * WOW_FLUTTER_ANALYSIS_TIME;
        int fft_capture_size = capture_size / 2;
        int wow_fft_capture_size = m_wow_flutter_capture_size / 2;
        
        m_fftinl = new double[capture_size];
        m_fftoutl = new fftw_complex[capture_size];
        m_fftinr = new double[capture_size];
        m_fftoutr = new fftw_complex[capture_size];
        m_fftdrawl = new double[fft_capture_size];
        m_fftdrawr = new double[fft_capture_size];
        m_fftfreqs = new double[fft_capture_size];   
        m_rms_fft = new double[fft_capture_size];
        m_current_window_cache = new double[capture_size];

        m_fftwowdrawfreqs = new double[wow_fft_capture_size]; 
        m_fftdrawwow = new double[wow_fft_capture_size];

        m_fftoutwow = new fftw_complex[m_wow_flutter_capture_size];

        m_wow_flutter_data.resize(m_wow_flutter_capture_size);
        m_wow_flutter_data_x.resize(m_wow_flutter_capture_size);

        int fft_flags = FFTW_PRESERVE_INPUT;

        if (m_optimized_fft) fft_flags |= FFTW_MEASURE;
        else fft_flags |= FFTW_ESTIMATE;

        m_wow_data_mutex.lock();
        m_longterm_audio.clear();
        m_wow_data_mutex.unlock();

        m_fftplanr   = fftw_plan_dft_r2c_1d(capture_size, m_fftinr, m_fftoutr, fft_flags);
        m_fftplanl   = fftw_plan_dft_r2c_1d(capture_size, m_fftinl, m_fftoutl, fft_flags);
        m_fftplanwow = fftw_plan_dft_r2c_1d(m_wow_flutter_capture_size, m_wow_flutter_data.data(), m_fftoutwow, fft_flags | FFTW_PRESERVE_INPUT);

        compute_fft_window_cache();
    }

    void reinit_recorder()
    {
        if (m_audio_in_idx < 0) return;
        if (m_audiomanager.get_input_sample_rates(m_audio_in_idx).empty()) return;

        if (m_in_sample_rate_idx >= m_audiomanager.get_input_sample_rates(m_audio_in_idx).size())
        {
            m_in_sample_rate_idx = 0;
            printf("Cannot set recorder samplerate to requested value\n");
        }
        int samplerate = m_audiomanager.get_input_sample_rates(m_audio_in_idx)[m_in_sample_rate_idx];

        if (m_audiorecorder.init(float(m_recorder_latency_ms) / 1000.f, m_audio_in_idx, samplerate))
        {
            m_audiorecorder.start();
        }

        init_capture();

        m_audiorecorder.pause(!m_compute_on);
    }

    void reset_sine_generator()
    {
        if (m_audiomanager.get_output_sample_rates(m_audio_out_idx).empty()) return;

        if (m_out_sample_rate_idx >= m_audiomanager.get_output_sample_rates(m_audio_out_idx).size())
        {
            m_out_sample_rate_idx = 0;
            printf("Cannot set player samplerate to requested value\n");
        }
        int current_sine_samplerate = m_audiomanager.get_output_sample_rates(m_audio_out_idx)[m_out_sample_rate_idx];
        m_sine_generator.destroy();
        m_sine_generator.init(m_audiomanager, m_audio_out_idx, current_sine_samplerate, m_sinegen_latency_s);
        m_sine_generator.set_pitch(m_pitch);
        m_sine_generator.start();
        m_sine_generator.pause(!m_sine_generator_switch);
    }

    void compute_fft_window_cache()
    {
        if (m_current_window_cache != nullptr) delete[] m_current_window_cache;
        m_current_window_cache = new double[m_capture_size];

        for(int i = 0; i < m_capture_size; ++i) m_current_window_cache[i] = m_window_fn(i, m_capture_size);
    }

    void compute_fft_window_corrections(int num_samples = 1000)
    {
        int tmp = m_fft_window_fn_index;
        double inv_num_samples = 1. / num_samples;
        for (int j = 0; j < 8; ++j)
        {
            m_fft_window_fn_index = j;
            set_window_fn(false);
            double sum = 0;
            double rms = 0;
            for (int i = 0; i < num_samples; i++)
            {
                double val = m_window_fn(i, num_samples);
                sum += val;
                rms += val*val;
            }

            // Normalization
            m_window_amplitude_correction[j] = 1.0 / (sum * inv_num_samples);
            m_window_energy_correction[j] = 1.0 / sqrt(rms * inv_num_samples);
        }
        // Restore
        m_fft_window_fn_index = tmp;
    }

    void set_theme()
    {
        if (m_uitheme == 0) ImGui::StyleColorsDark();   
        else if (m_uitheme == 1) ImGui::StyleColorsLight();   
        else if (m_uitheme == 2) ImGui::StyleColorsClassic();   
        
        ImGui::GetStyle().FrameRounding = 5.0;
        ImGui::GetStyle().ChildRounding = 5.0;
        ImGui::GetStyle().WindowRounding = 4.0;
        ImGui::GetStyle().GrabRounding = 4.0;
        ImGui::GetStyle().GrabMinSize = 4.0;
    }

    void check_settings_loaded()
    {
        static bool settings_loaded = false;
        if (!settings_loaded && GImGui->SettingsLoaded)
        {
            printf("Settings restored\n");
            settings_loaded = true;
            reset_audiomanager();
        }
    }

    void draw() override 
    {
        m_audiomanager.flush();
        check_settings_loaded();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Preferences"))
            {
                if (ImGui::BeginMenu("Theme"))
                {
                    if (ImGui::MenuItem("Dark", nullptr, nullptr))
                    {
                        m_uitheme = 0;
                        set_theme();
                    }
                    if (ImGui::MenuItem("Light", nullptr, nullptr))
                    {
                        m_uitheme = 1;
                        set_theme();
                    }
                    if (ImGui::MenuItem("Classic", nullptr, nullptr))
                    {
                        m_uitheme = 2;
                        set_theme();
                    }
                    ImGui::EndMenu();
                }
                ImGui::MenuItem("Sound card setup", nullptr, &m_sound_setup_open);
                ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
                if(ImGui::MenuItem("Optimized FFT compute", nullptr, &m_optimized_fft))
                {
                    reinit_recorder();
                }
                ImGui::PopItemFlag();
                
                //if(ImGui::MenuItem("Show Zscore settings", nullptr, nullptr))  m_show_zscore_settings = !m_show_zscore_settings;
                
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::BeginTabBar("MaintabBar");
        if (ImGui::BeginTabItem("Realtime analysis"))
        {
            draw_rt_analysis_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sweep measurement"))
        {
            draw_sweep_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("SDR analysis"))
        {
            draw_sdr();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();

        draw_tools_windows();
    }

    void start_sweep_gen()
    {
        m_compute_on = true;
        reinit_recorder();
        m_sweep_started = true;
        m_sweep_current_frequency = 20;
        m_sweep_freqs.clear();
        m_sweep_values.clear();
        m_sine_generator_switch = true;
        reset_sine_generator();
        m_sine_generator.set_pitch(m_sweep_current_frequency);
        m_sweep_timer.start();
        m_sweep_last_measure_freq = 10;
    }

    void stop_sweep_gen()
    {
        m_sweep_started = false;
        m_sine_generator_switch = false;
        m_sine_generator.pause();
        m_sweep_timer.stop();
    }

    void set_window_fn(bool compute_cache = true)
    {
        switch (m_fft_window_fn_index)
        {
            case 0:
                m_window_fn = rectangle_fft_window;
                break;
            case 1:
                m_window_fn = hamming_fft_window;
                break;
            case 2:
                m_window_fn = hann_poisson_fft_window;
                break;
            case 3:
                m_window_fn = blackman_fft_window;
                break;
            case 4:
                m_window_fn = blackman_harris_fft_window;
                break;
            case 5:
                m_window_fn = hann_fft_window;
                break;
            case 6:
                m_window_fn = kaiser5_fft_window;
                break;
            case 7:
                m_window_fn = kaiser7_fft_window;
                break;
            default:
                m_window_fn = rectangle_fft_window;
                break;
        }

        if (compute_cache) compute_fft_window_cache();
    }

    void draw_sweep_tab()
    {
        int channelcount = m_audiorecorder.get_channel_count(); 
        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        float frameh = ImGui::GetFrameHeightWithSpacing();
        float padh = 3.0f * ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ItemSpacing.y;

        ImGui::BeginChild("ScopesChild1", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);

        ImGui::BeginChild("ScopesChild2", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (!m_sweep_started)
        {
            if (ImGui::Button("Start"))
            {
                start_sweep_gen();
            }
            ImGui::SameLine();
            ImGui::ToggleButton("Async", &m_async_sweep);
        } else {
            if (ImGui::Button("Stop"))
            {
                stop_sweep_gen();
            }
        }
        ImGui::SetItemTooltip("Switch between synchronous and asynchronous capture");
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("FFTChildToneVol", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(70);
        if (ImGui::SliderInt("Tone power", &m_sine_volume_db, -100, 0, "%d dB"))
        {
            m_sine_generator.set_volume(m_sine_volume_db);
        }
        ImGui::SetItemTooltip("Set audio tone power");
        ImGui::EndChild();
        
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild3", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Log scale frequency", &m_logscale_frequency);
        ImGui::SetItemTooltip("Set log scale frequency axis");
        ImGui::EndChild();

        if (!m_async_sweep)
        {
            ImGui::SameLine();
            ImGui::BeginChild("ScopesChild4", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            ImGui::SetNextItemWidth(70);
            if(ImGui::DragInt("Delay (ms)", &m_measure_delay, 1.f, m_recorder_latency_ms * 2, 3000))
            {
                // Set a comfortable time amount to let the FFT settle (at leat 400ms for 200ms latency)
                m_sweep_timer.set(m_measure_delay);
            }
            ImGui::SetItemTooltip("Delay between tone change and capture, not set too low");
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("ScopesChildSpan", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            ImGui::SetNextItemWidth(70);
            ImGui::DragInt("Sweep capture #", &m_sweep_capture_num, 10.f, 10, 500);
            ImGui::SetItemTooltip("Number of measurement points");
            ImGui::EndChild();
        } else {
            ImGui::SameLine();
            ImGui::BeginChild("ScopesChildThresholdLevel", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            ImGui::SetNextItemWidth(70);
            ImGui::DragFloat("Threshold level (dB)", &m_sweep_threshold_level, 1., -100, 0);
            ImGui::SetItemTooltip("Number of measurement points");
            ImGui::EndChild();
        }

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChidWindowMode", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Window mode", &m_fft_window_fn_index, vector_getter, (void *)&m_wmodes, m_wmodes.size()))
        {
            set_window_fn();
        }
        ImGui::SetItemTooltip("Set the FFT window mode");
        ImGui::EndChild();
        if (channelcount > 1)
        {
            ImGui::SameLine();
            ImGui::BeginChild("ScopesChildChannelSelect", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            if (ImGui::Checkbox("Left", &m_fft_channel_left) )
            {
                m_fft_channel_right = !m_fft_channel_left;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("right", &m_fft_channel_right) )
            {
                m_fft_channel_left = !m_fft_channel_right;
            }
            ImGui::EndChild();
        }

        if (ImPlot::BeginPlot("AudioFFT", ImVec2(-1, -1)))
        {
            float xfftmax = current_sample_rate > 0 ? (current_sample_rate)/2.f : INFINITY;
            ImPlot::SetupAxes("Frequency", "dB FullScale", 0, ImPlotAxisFlags_Lock);
            if (m_logscale_frequency){
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
            }
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
            ImPlot::SetupAxesLimits(20.f, xfftmax, -130.0, 0.0);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 20.0, 24000.0);

            if (channelcount>0 && m_fftfreqs)
            {
                ImPlot::PlotLine("Audio FFT", m_fftfreqs, m_fft_channel_left  ?  m_fftdrawl : m_fftdrawr, m_sound_data_x.size()/2);
                double nf[4] = {0., (current_sample_rate)/2.0, m_noise_foor, m_noise_foor};
                ImPlot::PlotLine("Noise floor", nf, nf+2, 2);
                ImPlot::PlotLine("Frequency response", m_sweep_freqs.data(), m_sweep_values.data(), m_sweep_freqs.size());
            }

            float sweep_bar[4] = {(float)m_sweep_last_measure_freq, (float)m_sweep_last_measure_freq, 40.0, -200.0};
            ImPlot::PlotLine("Sweep", sweep_bar, sweep_bar+2, 2);
            
            ImPlot::EndPlot();
        }
        ImGui::EndChild();
    }

    void draw_sdr()
    {

        ImGui::BeginChild("ScopesChild1", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);

        ImGui::BeginChild("ScopesChild2", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (ImGui::Button("Start"))
        {
            
        }

        ImGui::EndChild();

        if (ImPlot::BeginPlot("SDR FFT", ImVec2(-1, -1)))
        {
            ImPlot::SetupAxes("Frequency (MHz)", "dBm", 0, ImPlotAxisFlags_Lock);
            // if (m_logscale_frequency){
            //     ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
            // }
            ImPlot::SetupAxesLimits(88, 108, -60.0, 40.0);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 88, 108);

            const std::vector<SDR_Scanner::Scan_result> scan_res = m_sdr_thread.get_scan_result();
            if (scan_res.size())
            {
                for (int i = 0; i < scan_res.size(); ++i)
                {
                    ImPlot::PlotLine("RF FFT", scan_res[i].buffer_x.data(), scan_res[i].buffer.data(), scan_res[i].buffer_x.size());
                }
            }

            ImPlot::EndPlot();
        }

        ImGui::EndChild();
    }

    void draw_lcd(const float value, const ImVec2 size, const int lcd_digits_size)
    {
        char voltmeter[10];
        snprintf(voltmeter, 10, "%.4f", value);

        ImGui::InvisibleButton("canvas", size);
        ImVec2 p0 = ImGui::GetItemRectMin();
        ImVec2 p1 = ImGui::GetItemRectMax();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->PushClipRect(p0, p1);

        int textsize = strlen(voltmeter);
        float p=0.02*size.x,s=size.x/lcd_digits_size-p,x=s*.5,y=size.y*.5;
        for(int i=(textsize-1) - (textsize - lcd_digits_size);i>=0;i--)
        {
            if (voltmeter[i] >= '0' && voltmeter[i] <= '9')
            {
                int _d = voltmeter[i] - '0';
                digit(draw_list,_d,ImVec2(s*.5,y),ImVec2(p1.x-x,p0.y+y));
                x+=s+p;
            } else {
                draw_list->AddCircleFilled(ImVec2(p1.x-x,p0.y+(2.f*y) -12.f), 4.f, lcd_fg, 8);
                x+=s/2+p;
            }
        }
        draw_list->PopClipRect();
    }

    void draw_rt_analysis_tab()
    {
        int channelcount = m_audiorecorder.get_channel_count(); 
        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        bool must_reinit_recorder = false;

        float frameh = ImGui::GetFrameHeightWithSpacing();
        float padh = 3.0f * ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ItemSpacing.y;

        /*
        * Tone Generator
        */
        if (!m_sweep_started)
        {
            ImGui::BeginChild("ScopesChildToneGen", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);

            ImGui::BeginChild("ScopesChildTonGenSwitch", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            if(ImGui::ToggleButton("Tone generator", &m_sine_generator_switch))
            {
                reset_sine_generator();
            }
            ImGui::SetItemTooltip("Sine generator ON/OFF");

            ImGui::SameLine();
            if (ImGui::SliderInt("Pitch", &m_pitch, 20, 20000))
            {
                m_sine_generator.set_pitch(m_pitch);
            }
            ImGui::SetItemTooltip("Set the pitch of the sine generator");
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("ScopesChildToneVol", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            if (ImGui::SliderInt("Intensity", &m_sine_volume_db, -100, 0, "%d dB"))
            {
                m_sine_generator.set_volume(m_sine_volume_db);
            }
            ImGui::SetItemTooltip("Set the generator intensity");
            ImGui::EndChild();

            // ImGui::SameLine();
            // if(ImGui::Button("test"))
            // {
            //     MainWindow2* win = new MainWindow2;
            //     App_SDL::get()->add_window(win);
            // }

            ImGui::EndChild();
        }


        /*
        * Time domain analysis
        */
        ImGui::BeginChild("ScopesChildMain", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
        float plotheight = height() / 2.0f - 5.f;

        ImGui::BeginChild("ScopesChildBar", ImVec2(0, plotheight), ImGuiChildFlags_Border, ImGuiWindowFlags_None);

        ImGui::BeginChild("ScopesChildCaptureOnOff", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (ImGui::ToggleButton("Start", &m_compute_on))
        {
            m_audiorecorder.pause(!m_compute_on);
        }
        ImGui::SetItemTooltip("Start realtime capture");
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("ScopesChildShowRmsVolts", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Show voltmeter", &m_show_rms_voltage);
        ImGui::SetItemTooltip("Shows the voltmeters panel");
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("ScopesChildShowWAF", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (ImGui::ToggleButton("W&F", &m_show_wow_flutter))
        {
            while(App_SDL::get()->get_thread("WFtask"))
            {
                App_SDL::get()->release_finished_threads();
            }

            m_longterm_audio.clear();
            //m_wow_flutter_data.clear();
            //m_wow_flutter_data_x.clear();
        }
        ImGui::SetItemTooltip("Shows wow and flutter panel");
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildCalib", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        static double rms_calibration = 1.0;
        if (m_rms_calibration_scale == 1.0)
        {
            ImGui::SetNextItemWidth(70);
            ImGui::InputDouble("Measured RMS", &rms_calibration);
            ImGui::SetItemTooltip("Enter the measured RMS voltage here to calibrate the meters/graph");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            if (ImGui::Button("Calibrate from left"))
            {

                m_rms_calibration_scale = rms_calibration / m_rms_left;
            }
            ImGui::SetItemTooltip("Do the calibration from left channel");
            if (channelcount > 1)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                if (ImGui::Button("Calibrate from right"))
                {
                    m_rms_calibration_scale = rms_calibration / m_rms_right;
                }
                ImGui::SetItemTooltip("Do the calibration from right channel");
            }
        }
        if (m_rms_calibration_scale != 1.0)
{
            ImGui::SameLine();
            if (ImGui::Button("Clear calibration"))
            {

                m_rms_calibration_scale = 1.0;
                rms_calibration = 1.0;
            }
            ImGui::SetItemTooltip("Clear the calibration");
        }
        ImGui::EndChild();

        /*
        *   0dBu Ref section 
        */

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildShow0db", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Show 0dBu Ref", &m_show0db);
        ImGui::SetItemTooltip("Shows the 0dB (775mV or 1mW/600ohms) on the graph");
        ImGui::EndChild();

        /*
        *  X dB target section
        */

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildTargetVolt", ImVec2(-1, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        const char* items[] = {"left", "right"};
        ImGui::ToggleButton("dB target", &m_use_targetdb);
        ImGui::SetItemTooltip("Set a target dB value relative to current measure");
        if (m_use_targetdb)
        {
            if (!m_lockdb)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                ImGui::Combo("Channel", &m_current_db_target_channel, items, 2);
                ImGui::SetItemTooltip("Which channel to work on");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                ImGui::SliderFloat("dB", &m_target_db, -20, 20);
                ImGui::SetItemTooltip("Target dB value");
            }
            ImGui::SameLine();
            ImGui::ToggleButton("Lock", &m_lockdb);
            ImGui::SetItemTooltip("Lock the current measure");
        }
        ImGui::EndChild();

        /*
        *   LCD voltmeter
        */
        if (m_show_rms_voltage)
        {
            if (m_rms_calibration_scale == 1.0)
            {
                lcd_fg = IM_COL32(200,0,0,255);
            } else {
                lcd_fg = IM_COL32(0,200,0,255);
            }
            ImGui::BeginChild("ScopesChildVoltageLcd", ImVec2(0, -1), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeX  | ImGuiWindowFlags_None);
             
            ImGui::BeginChild("ScopesChildVoltageLcd1", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
            TextCenter("Volts RMS Left");
            draw_lcd(m_rms_left * m_rms_calibration_scale, ImVec2(180, 60), 6);
            if (m_rms_calibration_scale != 1.){
                double db = 20. * log10(m_rms_left * m_rms_calibration_scale / .775);
                TextCenter("[%.2f dBu]", db);
            }
            if (m_lockdb && m_current_db_target_channel == 0)
            {
                float target_val_left = 1.f - fabs( m_locked_db_value - m_rms_left * m_rms_calibration_scale ) * 10.f;
                ImGui::ProgressBar(target_val_left);
            }
            ImGui::EndChild();

            //ImGui::SetCursorPosY(height());
            if (channelcount > 1)
            {
                ImGui::BeginChild("ScopesChildVoltageLcd2", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
                TextCenter("Volts RMS Right");
                draw_lcd(m_rms_right * m_rms_calibration_scale, ImVec2(180, 60), 6);
                if (m_rms_calibration_scale != 1.){
                    double db = 20. * log10(m_rms_right * m_rms_calibration_scale / .775);
                    TextCenter("[%.2f dBu]", db);
                }
                if (m_lockdb && m_current_db_target_channel == 1)
                {
                    float target_val_right = 1.f - fabs( m_locked_db_value - m_rms_right * m_rms_calibration_scale ) *10.f;
                    ImGui::ProgressBar(target_val_right);
                }
                ImGui::EndChild();
            }

            ImGui::BeginChild("ScopesChildFreqCounter", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
            //double freq = m_fftfreqs ? m_fftfreqs[m_fft_highest_idx[m_fundamental_index]] / 1000. : 0.0;
            TextCenter("Frequency KHz");
            lcd_fg = IM_COL32(0,200,0,255);
            draw_lcd(m_frequency_counter / 1000., ImVec2(180, 60), 6);
            ImGui::EndChild();

            ImGui::EndChild();
            ImGui::SameLine();
        }

        if (ImPlot::BeginPlot("Audio", ImVec2(m_show_wow_flutter ? width()-plotheight*1.5-10 : -1, -1)))
        {
            double x_limit = 1.0f / m_scopezoom;
            double xmax = current_sample_rate > 0 ? float(m_capture_size) * (1.0 / (current_sample_rate * 0.001)) : INFINITY;
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, xmax);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -x_limit, x_limit, ImPlotCond_Always);

            if (m_rms_calibration_scale != 1.0)
            {
                ImPlot::SetupAxis(ImAxis_Y2, "Volts");
                ImPlot::SetupAxisLimits(ImAxis_Y2, -m_rms_calibration_scale / m_scopezoom, m_rms_calibration_scale / m_scopezoom, ImPlotCond_Always);
            }
            ImPlot::SetupAxis(ImAxis_X1, "Time [milliseconds]", 0);
            ImPlot::SetupAxis(ImAxis_Y1, "Amplitude [dB FullScale]", ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_Opposite | ImPlotAxisFlags_Lock);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, xmax);

            if (ImPlot::IsAxisHovered(ImAxis_Y1) || ImPlot::IsAxisHovered(ImAxis_Y2)){
                // Zoom Y axis in/out
                m_scopezoom += ImGui::GetIO().MouseWheel * .1;
                if (m_scopezoom < 1) m_scopezoom = 1;
                if (m_scopezoom > 50) m_scopezoom = 50;
            }
            
            if (channelcount > 0) ImPlot::PlotLine("Left channel", m_sound_data_x.data(), m_sound_data1.data(), m_sound_data_x.size());
            if (channelcount > 1) ImPlot::PlotLine("Right channel", m_sound_data_x.data(), m_sound_data2.data(), m_sound_data_x.size());
            
            char rmstext[20];
            ImVec2 plotpos  = ImPlot::GetPlotPos();
            ImVec2 plotsize = ImPlot::GetPlotSize();

            if (channelcount > 0)
            {
                double rms[4] = {0., (current_sample_rate)/2.0, m_rms_left, m_rms_left};
                ImPlot::PlotLine("signal RMS left", rms, rms+2, 2);
            }
            if (channelcount > 1)
            {
                double rms[4] = {0., (current_sample_rate)/2.0, m_rms_right, m_rms_right};
                ImPlot::PlotLine("signal RMS right", rms, rms+2, 2);
            }

            if(m_use_targetdb)
            {
                if (!m_lockdb) m_locked_db_value = ((m_current_db_target_channel == 0) ? m_rms_left : m_rms_right) * pow(10, m_target_db/20.0);
                double tgtpnt[4] = {0., (current_sample_rate)/2.0, m_locked_db_value, m_locked_db_value};
                ImPlot::PlotLine("target dB", tgtpnt, tgtpnt+2, 2);
            }

            if (m_show0db)
            {
                double zerodb = .775 / m_rms_calibration_scale;
                double rms[4] = {0., (current_sample_rate)/2.0, zerodb, zerodb};
                ImPlot::PlotLine("0 dB Reference", rms, rms+2, 2);
            }

            ImPlot::EndPlot();
        }
        
        ImGui::SameLine();
        if (m_show_wow_flutter)
        {
            const char* ref_freq_presets[] = {"3000","3150", "Custom"};
            const char* filter_presets[] = {"Disabled", "Wow (6Hz)","Flutter low (20Hz)", "Flutter high (100Hz)"};
            float frequency;
            static bool fft_view = false;
            
            ImGui::BeginChild("ChildWF", ImVec2(0, -1), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);

            ImGui::BeginChild("ChildFFTControl", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
            ImGui::ToggleButton("FFT view", &fft_view);
            ImGui::EndChild();

            if (!fft_view)
            {
                ImGui::SameLine();
                ImGui::BeginChild("ChildWFControl", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
                ImGui::SetNextItemWidth(80);
                ImGui::Combo("Reference frequency", &m_wow_test_frequency, ref_freq_presets, 3);
                if (m_wow_test_frequency == 2)
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputInt("Custom frequency", &m_wow_test_frequency_custom, 1, 100);
                    if (m_wow_test_frequency_custom < 1000) m_wow_test_frequency_custom = 1000;
                    if (m_wow_test_frequency_custom > 10000) m_wow_test_frequency_custom = 10000;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160);
                ImGui::Combo("Low pass filter (Hz)", &m_wf_filter_freq_combo, filter_presets, 4);
                ImGui::EndChild();
            }

            if (channelcount > 1)
            {
                ImGui::SameLine();
                ImGui::BeginChild("ScopesChildChannelSelect", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
                if (ImGui::Checkbox("Left", &m_fft_channel_left) )
                {
                    m_fft_channel_right = !m_fft_channel_left;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("right", &m_fft_channel_right) )
                {
                    m_fft_channel_left = !m_fft_channel_right;
                }
                ImGui::SetItemTooltip("Analyse right channel");
                ImGui::EndChild();
            }

            static float max_freq = 200;
            static float max_fft_freq = 50;

            if (m_wow_test_frequency == 0) frequency = 3000;
            else if (m_wow_test_frequency == 1) frequency = 3150;
            else if (m_wow_test_frequency == 2) frequency = m_wow_test_frequency_custom;

            float max_percent = (max_freq / frequency) * 100.;

            if(!fft_view && ImPlot::BeginPlot("Wow and flutter analysis (unweighted)", ImVec2(plotheight*1.5, -1)))
            {
                ImPlot::SetupAxes("Time (seconds)", "Freqency drift (Hz)", 0, ImPlotAxisFlags_Lock);
                ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0., 5.);
                ImPlot::SetupAxisLimits(ImAxis_X1, 0., 5., 0);
                ImPlot::SetupAxisLimits(ImAxis_Y1, -max_freq, max_freq, ImPlotCond_Always);
                ImPlot::SetupAxis(ImAxis_Y2, "Peak drift %", ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines);
                ImPlot::SetupAxisLimits(ImAxis_Y2, -max_percent, max_percent, ImPlotCond_Always);

                if (ImPlot::IsAxisHovered(ImAxis_Y1) || ImPlot::IsAxisHovered(ImAxis_Y2)){
                    // Zoom Y axis in/out
                    max_freq += ImGui::GetIO().MouseWheel * -10;
                    if (max_freq < 10) max_freq = 10;
                    if (max_freq > 500) max_freq = 500;
                }
                
                m_wow_data_mutex.lock();
                    ImPlot::PlotLine("Wow and flutter", m_wow_flutter_data_x.data(), m_wow_flutter_data.data(), m_wow_flutter_data.size());
                    
                    double wow_mean_bar[4] = {0., 5., m_wow_mean, m_wow_mean};
                    ImPlot::PlotLine("Wow & flutter mean", wow_mean_bar, wow_mean_bar+2, 2);

                    float peak_percent = (m_wow_peak_detection / (frequency + m_wow_mean)) * 100.;
                    float freq_drift = (m_wow_mean / frequency) * 100.;
                m_wow_data_mutex.unlock();

                char peak_text[64];
                snprintf(peak_text, 32, "W&F Peak: %.3f %%", peak_percent);
                ImVec2 plotpos = ImPlot::GetPlotPos();
                ImVec2 plotsize = ImPlot::GetPlotSize();
                ImPlotPoint pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.1)));
                ImPlot::PlotText(peak_text, pnt.x, pnt.y);
                snprintf(peak_text, 32, "Frequency drift: %.3f %%", freq_drift);
                pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.2)));
                ImPlot::PlotText(peak_text, pnt.x, pnt.y);
                ImPlot::EndPlot();
            }
            else
            if (ImPlot::BeginPlot("Wow and flutter FFT analysis", ImVec2(plotheight*1.5, -1)))
            {
                const double max_frequency = current_sample_rate / WOW_FLUTTER_DECIMATION / 2.;
                ImPlot::SetupAxes("Frequency (Hz)", "Freqency drift (Hz)", 0, ImPlotAxisFlags_Lock);
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_SymLog);
                ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0.1, max_frequency);
                ImPlot::SetupAxisLimits(ImAxis_X1, 0.1, max_frequency, 0);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_fft_freq, ImPlotCond_Always);

                if (ImPlot::IsAxisHovered(ImAxis_Y1) || ImPlot::IsAxisHovered(ImAxis_Y2)){
                    // Zoom Y axis in/out
                    max_fft_freq += ImGui::GetIO().MouseWheel * -10;
                    if (max_freq < 10) max_fft_freq = 10;
                    if (max_freq > 500) max_fft_freq = 500;
                }

                m_wow_data_mutex.lock();
                    ImPlot::PlotLine("Frequency drift", m_fftwowdrawfreqs, m_fftdrawwow, m_wow_flutter_capture_size / 2);
                m_wow_data_mutex.unlock();
                ImPlot::EndPlot();
            }
            ImGui::EndChild();
        }

        ImGui::EndChild();

        /*
        * Frequency domain analysis
        */
        ImGui::BeginChild("fftscopechild", ImVec2(0, -1), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
        ImGui::BeginChild("ScopesChildButtons", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);

        ImGui::BeginChild("ScopesChildLogScaleSelect", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Log scale frequency", &m_logscale_frequency);
        ImGui::SetItemTooltip("Log scale/linear scale X axis");
        ImGui::EndChild();

        ImGui::SameLine();
        if (channelcount > 1)
        {
            ImGui::BeginChild("ScopesChild5", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            ImGui::ToggleButton("Compute L/R phase", &m_compute_channel_phase);
            ImGui::SetItemTooltip("Enable left/right phase/amplitude differential");
            ImGui::EndChild();
        }
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildFFTWindowMode", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Window mode", &m_fft_window_fn_index, vector_getter, (void *)&m_wmodes, m_wmodes.size()))
        {
            set_window_fn();
        }
        ImGui::SetItemTooltip("Set the FFT windowing mode");
        ImGui::EndChild();
        ImGui::SameLine();
        if (channelcount > 1)
        {
            ImGui::BeginChild("ScopesChildChannelSelect", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            if (ImGui::Checkbox("Left", &m_fft_channel_left) )
            {
                m_fft_channel_right = !m_fft_channel_left;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("right", &m_fft_channel_right) )
            {
                m_fft_channel_left = !m_fft_channel_right;
            }
            ImGui::SetItemTooltip("Analyse right channel");
            ImGui::EndChild();
        }

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildCaptureSize", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(100);
        if(ImGui::InputInt("Capture size (ms)", &m_recorder_latency_ms, 100, 200, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if(m_recorder_latency_ms < 100) m_recorder_latency_ms = 100;
            if(m_recorder_latency_ms > 1000) m_recorder_latency_ms = 1000;
            must_reinit_recorder = true;
        }
        ImGui::SetItemTooltip("Audio sampling time in millisecond");
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ShowThdChild", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (ImGui::ToggleButton("Show THD", &m_show_thd))
        {
            static int old_capture_time = 100;
            if (m_show_thd){
                old_capture_time = m_recorder_latency_ms;
                if (m_recorder_latency_ms < 500)
                {
                    m_recorder_latency_ms = 500;
                    must_reinit_recorder = true;
                }
            }
            else
            {
                m_recorder_latency_ms = old_capture_time;
                must_reinit_recorder = true;
            }
        }
        ImGui::SetItemTooltip("Enable HD overlay");
        ImGui::EndChild();

        // if (m_show_zscore_settings && m_show_thd)
        // {
        //     ImGui::SameLine();
        //     ImGui::BeginChild("ScopesChildZScore", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        //     ImGui::SetNextItemWidth(50);
        //     ImGui::SliderInt("ZscoreLag", &m_zscore_lag, 5, 500);
        //     ImGui::SetItemTooltip("Set the length of the Z-score algorithm (for peak detection)");
        //     ImGui::SameLine();
        //     ImGui::SetNextItemWidth(50);
        //     ImGui::SliderFloat("ZscoreInfl.", &m_zscore_influence, 0., 1.);
        //     ImGui::SetItemTooltip("Influence of Z-score");
        //     ImGui::SameLine();
        //     ImGui::SetNextItemWidth(50);
        //     ImGui::SliderFloat("ZscoreThres.", &m_zscore_threshold, 0.5, 100.);
        //     ImGui::SetItemTooltip("Threshold of Z-score");
        //     ImGui::EndChild();
        // }
        ImGui::EndChild();

        if (ImPlot::BeginPlot("Audio FFT", ImVec2(m_compute_channel_phase ? width() - plotheight * 1.5f - 10 : -1, -1)))
        {
            bool calibration_active = m_rms_calibration_scale != 1.0;
            double* fft_draw = m_fft_channel_left  ? m_fftdrawl : m_fftdrawr;
            float xfftmax = current_sample_rate > 0 ? (current_sample_rate)/2.f : INFINITY;
            ImPlot::SetupLegend(ImPlotLocation_NorthEast);
            ImPlot::SetupAxis(ImAxis_X1, "Frequency", 0);
            ImPlot::SetupAxis(ImAxis_Y1, "dB FullScale", ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_Lock | ImPlotAxisFlags_Opposite);
            if (m_logscale_frequency)
            {
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
            }
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
            ImPlot::SetupAxesLimits(20.f, xfftmax, -120.0, 20.0);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 20.f, 24000.f);

            double diffdb = 0;
            if (calibration_active)
            {
                diffdb = 20.0 * log10(m_rms_calibration_scale);
                ImPlot::SetupAxis(ImAxis_Y2, "dBu", 0);
                ImPlot::SetupAxisLimits(ImAxis_Y2, -120 + diffdb, 20 + diffdb, ImPlotCond_Always);
            }

            if (m_fftfreqs && m_show_thd)
            {
                ImPlot::PlotShaded("Fundamental detection", (double*)&m_fftfreqs[m_fft_fund_idx_range_min+1], (double*)&fft_draw[m_fft_fund_idx_range_min+1], m_fft_fund_idx_range_max - m_fft_fund_idx_range_min, -200.0);
                
                char thdtext[64];
                snprintf(thdtext, 32, "THD : %.3f %%", m_thd);
                ImVec2 plotpos = ImPlot::GetPlotPos();
                ImVec2 plotsize = ImPlot::GetPlotSize();
                float plot_to_pix_graph = 140. / plotsize.y;
                ImPlotPoint pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.1)));
                ImPlot::PlotText(thdtext, pnt.x, pnt.y);
                pnt.y -= 20 * plot_to_pix_graph;
                snprintf(thdtext, 32, "THD+N : %.3f %% (%.2f dB)", m_thdn, m_thddb);
                ImPlot::PlotText(thdtext, pnt.x, pnt.y);
                pnt.y -= 20 * plot_to_pix_graph;
                snprintf(thdtext, 32, "Total RMS : %.4f", m_fft_rms * m_rms_calibration_scale);
                ImPlot::PlotText(thdtext, pnt.x, pnt.y);

                for (int i = m_fundamental_index; i < m_fft_found_peaks; ++i)
                {
                    double fund[4] = {m_fft_highest_pos[i], m_fft_highest_pos[i], 40.0, -200.0};
                    ImPlot::PlotLine("Peaks", fund, fund+2, 2);
                    double y_pos = fft_draw[m_fft_highest_idx[i]];
                    if (!calibration_active)
                    {
                        snprintf(thdtext, 16, "%.4fdB", y_pos);
                    }
                    else
                    {
                        double y_pos2 = y_pos + diffdb;
                        snprintf(thdtext, 16, "%.4fdBu", y_pos2);
                    } 
                    ImPlot::PlotText(thdtext, m_fft_highest_pos[i], y_pos+40 * plot_to_pix_graph);
                    double freq = m_fftfreqs[m_fft_highest_idx[i]] / 1000.0;
                    snprintf(thdtext, 16, "%.4fKHz", freq);
                    ImPlot::PlotText(thdtext, m_fft_highest_pos[i], y_pos+20 * plot_to_pix_graph);
                }

            }


            double nf[4] = {0., (current_sample_rate)/2.0, m_noise_foor, m_noise_foor};
            ImPlot::PlotLine("Noise floor", nf, nf+2, 2);
            
            if (channelcount>0 && m_fftfreqs)
            {
                if (m_fft_channel_left) ImPlot::PlotLine("Audio left FFT", m_fftfreqs, m_fftdrawl, m_sound_data_x.size()/2);
                if (m_fft_channel_right) ImPlot::PlotLine("Audio right FFT", m_fftfreqs, m_fftdrawr, m_sound_data_x.size()/2);
            }

            ImPlot::EndPlot();
        }
        if (m_compute_channel_phase)
        {
            static float phase_limit_mult = 1.f;
            static float amplitude_limit_mult = 1.f;
            ImGui::SameLine();
            if (ImPlot::BeginPlot("L/R Phase & Amplitude diff", ImVec2(plotheight * 1.5, -1)))
            {
                ImPlot::SetupAxis(ImAxis_Y1, "Phase (degrees)");
                ImPlot::SetupAxis(ImAxis_Y2, "dB", ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines);
                ImPlot::SetupAxis(ImAxis_X1, "Time");

                ImPlot::SetupAxesLimits(0.f, m_phase_time.size(), -180.0 * phase_limit_mult, 180.0 * phase_limit_mult, ImPlotCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y2, -20. * amplitude_limit_mult, 20. * amplitude_limit_mult, ImPlotCond_Always);

                if (ImPlot::IsAxisHovered(ImAxis_Y1)){
                    // Zoom Y axis in/out
                    phase_limit_mult += ImGui::GetIO().MouseWheel * .01;
                    if (phase_limit_mult < .1) phase_limit_mult = .1;
                    if (phase_limit_mult > 1) phase_limit_mult = 1;
                }

                if (ImPlot::IsAxisHovered(ImAxis_Y2)){
                    // Zoom Y axis in/out
                    amplitude_limit_mult += ImGui::GetIO().MouseWheel * .1;
                    if (amplitude_limit_mult < .1) amplitude_limit_mult = .1;
                    if (amplitude_limit_mult > 10) amplitude_limit_mult = 10;
                }

                ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
                ImPlot::PlotLine("L/R phase", &m_phase_time[0], &m_phase_history[0], m_phase_time.size());
                ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
                ImPlot::PlotLine("Amplitude diff", &m_phase_time[0], &m_lrdiff_history[0], m_phase_time.size());
                ImPlot::EndPlot();
            }
        }

        ImGui::EndChild();

        ImGui::EndChild();

        if (must_reinit_recorder)
        {
            if (m_measure_delay < m_recorder_latency_ms * 2)
            {
                m_measure_delay = m_recorder_latency_ms * 2;
                m_sweep_timer.set(m_measure_delay);
            }
            reinit_recorder();
        }
    }

    bool compute(bool compute_fft = true, bool compute_noise_floor = true)
    {
        const int channelcount = m_audiorecorder.get_channel_count();
        if (channelcount == 0)
        {
            return false;
        }

        bool data_available = m_audiorecorder.get_data(m_raw_buffer, m_capture_size * channelcount);
        if (!data_available){
            return false;
        }

        const int fft_capture_size = m_capture_size / 2;
        const double current_sample_rate = m_audiorecorder.get_current_samplerate();
        const double half_sample_rate = current_sample_rate / 2.0;
        const double inv_current_sample_rate = 1.0 / current_sample_rate;
        const double inv_fft_capture_size = 1.0 / float(fft_capture_size);
        const double fft_step = half_sample_rate * inv_fft_capture_size;
        m_fft_highest_val = -100;
        
        if (m_sound_data1.size() != m_capture_size) m_sound_data1.resize(m_capture_size);
        if (m_sound_data2.size() != m_capture_size) m_sound_data2.resize(m_capture_size);
        if (m_sound_data_x.size() != m_capture_size) m_sound_data_x.resize(m_capture_size);

        m_rms_left = m_rms_right = 0.0;
        double twopif_over_sr = 2. * M_PI / current_sample_rate;

        // Fill audio waveform
        for (int i = 0; i < m_capture_size; i++)
        {
            //double sound_data = .3 * sin(3145.*double(i) * twopif_over_sr);//
            double sound_data = m_raw_buffer[i*channelcount] * m_audio_gain;
            m_sound_data1[i] = sound_data;
                
            // THD test for non linear signal by applying small odd harmomics distortion
            m_fftinl[i] = sound_data * m_current_window_cache[i];
            m_sound_data_x[i] = float(i) * inv_current_sample_rate * 1000.0;

            m_rms_left += m_sound_data1[i] * m_sound_data1[i];
            if(channelcount>1)
            {
                m_sound_data2[i] = m_raw_buffer[i*channelcount+1] * m_audio_gain;
                m_fftinr[i] = m_sound_data2[i] * m_current_window_cache[i];
                m_rms_right += m_sound_data2[i] * m_sound_data2[i];
            }
        }

        if (m_show_wow_flutter){
            // Audio data ready, launch W&F measurement as soon as possible in parallel
             compute_wow_and_flutter();
        }

        detect_periods();

        m_rms_left = m_rms_left / m_capture_size;
        m_rms_left = sqrt(m_rms_left);

        if (channelcount > 1)
        {
            m_rms_right = m_rms_right / m_capture_size;
            m_rms_right = sqrt(m_rms_right);
        }
        
        if (compute_fft)
        {
            double* current_fft_draw = m_fft_channel_left  ? m_fftdrawl : m_fftdrawr;
            // Compute and fill audio FFT
            ::fftw_execute(m_fftplanl);
            if (channelcount > 1) ::fftw_execute(m_fftplanr);

            std::vector<double> fftdatal(fft_capture_size);
            float sum = 0;
            for (int i = 0; i < fft_capture_size; ++i)
            {
                m_fftfreqs[i] = fft_step * (double)(i);
                double fftout = sqrt(m_fftoutl[i][0] * m_fftoutl[i][0] + m_fftoutl[i][1] * m_fftoutl[i][1]) * inv_fft_capture_size;
                fftout *= m_window_amplitude_correction[m_fft_window_fn_index];
                fftout = std::max(20.0 * log10(fftout), -200.0);
                m_fftdrawl[i] = isnan(fftout) ? -200.f : fftout;
                if (m_fft_channel_left) sum += fftout;

                if (channelcount > 1)
                {
                    double fftout = sqrt(m_fftoutr[i][0] * m_fftoutr[i][0] + m_fftoutr[i][1] * m_fftoutr[i][1]) * inv_fft_capture_size;
                    fftout *= m_window_amplitude_correction[m_fft_window_fn_index];
                    fftout = std::max(20.0 * log10(fftout), -200.0);
                    m_fftdrawr[i] = isnan(fftout) ? -200.f : fftout;
                    if (m_fft_channel_right) sum += fftout;
                }
            }

            if (compute_noise_floor)
            {
                double mean = sum * inv_fft_capture_size;
                double stddev = 0;
                for (int i = 0; i < fft_capture_size; ++i)
                {
                    double a = (current_fft_draw[i] - mean);
                    stddev += a * a;
                }
                stddev = sqrt(stddev / float(fft_capture_size - 1));
                m_noise_foor = mean + stddev;
            } // compute_noise_floor
        } // compute_fft

        return true;
    }

    void compute_wow_and_flutter()
    {
        double samplerate = m_audiorecorder.get_current_samplerate();

                // Append captured audio data to get them
        int audio_capture_length = WOW_FLUTTER_ANALYSIS_TIME * samplerate;
        int sampled_audio_length = m_sound_data1.size();

        m_wow_data_mutex.lock();
        if (m_longterm_audio.empty())
        {
            m_longterm_audio.reserve(audio_capture_length);
        }

        if (m_longterm_audio.size() < audio_capture_length)
        {
            m_longterm_audio.insert(m_longterm_audio.end(), m_sound_data1.begin(), m_sound_data1.end());
        }
        else
        {
            int move_size = audio_capture_length - sampled_audio_length;
            memcpy(&m_longterm_audio[0], &m_longterm_audio[sampled_audio_length], move_size*sizeof(double));
            memcpy(&m_longterm_audio[move_size], &m_sound_data1[0], sampled_audio_length*sizeof(double));
        }
        m_wow_data_mutex.unlock();

        if (m_longterm_audio.size() < audio_capture_length)
        {
            // Wait buffer to be filled
            return;
        }

        App_SDL::get()->release_finished_threads();
        if(App_SDL::get()->get_thread("WFtask")){
            // Check is previous thread has terminated, if not, reject these samples to not overload
            return;
        }

        int reference_frequency = 3000;
        if (m_wow_test_frequency == 1) reference_frequency = 3150;
        else if (m_wow_test_frequency == 2) reference_frequency = m_wow_test_frequency_custom;

        // Launch thread
        WowAndFluterThread* wt = new WowAndFluterThread(samplerate, m_longterm_audio,
            m_wow_flutter_data, m_wow_flutter_data_x, m_fft_channel_left ? m_sound_data1 : m_sound_data2, reference_frequency,
            m_wow_data_mutex, WOW_FLUTTER_ANALYSIS_TIME, m_wf_filter_freq_combo,
            m_wow_peak_detection, m_wow_mean, WOW_FLUTTER_DECIMATION, m_fftdrawwow, m_fftoutwow, m_fftwowdrawfreqs, m_fftplanwow);
        wt->start();
    }

    void compute_thdn()
    {
        int fft_capture_size = m_capture_size/2;
        fftw_complex * current_fft = m_fft_channel_left ? m_fftoutl : m_fftoutr;
        const double invsqrt2 = 1.0 / sqrt(2.0);
        const double inv_capture_size = 1.0 / (double(fft_capture_size));

        m_thdn = m_thddb = 0.;

        double max_val = -200;
        int max_val_index = 0;

        m_fft_rms = 0;
        for (int i = 1; i < fft_capture_size; ++i)
        {
            double fft_module = sqrt(current_fft[i][0] * current_fft[i][0] + current_fft[i][1] * current_fft[i][1]);
            fft_module *= m_window_energy_correction[m_fft_window_fn_index];
            fft_module *= fft_module;
            m_fft_rms += fft_module;
            m_rms_fft[i] = fft_module;
            
            if (fft_module > max_val)
            {
                max_val = fft_module;
                max_val_index = i;
            }
        }
        m_fft_rms = sqrt(m_fft_rms) * invsqrt2 * inv_capture_size;
        //printf("RMS = %f\n", total_rms);

        // Find FFT fundamental range
        double tmp = max_val;
        for (int i = max_val_index; i < fft_capture_size; ++i)
        {
            if (m_rms_fft[i] > tmp)
            {
                m_fft_fund_idx_range_max = i;
                break;
            }
            tmp = m_rms_fft[i];
        }
        
        tmp = max_val;
        for (int i= max_val_index; i >= 0; --i)
        {
            if (m_rms_fft[i] > tmp)
            {
                m_fft_fund_idx_range_min = i;
                break;
            }
            tmp = m_rms_fft[i];
        }

        if (m_fft_fund_idx_range_max - m_fft_fund_idx_range_min <=0)
        {
            m_fft_fund_idx_range_max = m_fft_fund_idx_range_min = 0;
            return;
        }

        double noise_rms = 0;
        // Start at 1, we don't want DC value
        for (int i = 1; i < m_fft_fund_idx_range_min; ++i)
        {
            noise_rms += m_rms_fft[i];
        }

        for (int i = m_fft_fund_idx_range_max; i < fft_capture_size; ++i)
        {
            noise_rms += m_rms_fft[i];
        }

        noise_rms = sqrt(noise_rms) * invsqrt2 * inv_capture_size;

        double noise_db = 20. * log10(noise_rms);

        m_thdn = noise_rms / m_fft_rms;
        m_thddb = 20.0 * log10(m_thdn);
        m_thdn *= 100.0;
    }

    void compute_thd()
    {
        double* current_fft_draw = m_fft_channel_left ? m_fftdrawl : m_fftdrawr;
        const int fft_capture_size = m_capture_size / 2;
        m_fft_found_peaks = 1;

        // Find max values of filtered signal
        int fundamental_index = 0;
        double max = -200.;
        for(int i = 0; i < fft_capture_size; ++i)
        {
            if (current_fft_draw[i] > max)
            {
                max = current_fft_draw[i];
                fundamental_index = i; 
            }
        }

        m_fundamental_index = 0;

        int fundamental_frequency = m_fftfreqs[fundamental_index]; 

        m_fft_highest_idx[0] = fundamental_index;
        m_fft_highest_pos[0] = m_fftfreqs[fundamental_index];

        for (int i = 1; i < 8; ++i)
        {
            int i_order_harmonic = fundamental_index * (i+1);
            if (i_order_harmonic > fft_capture_size) break;
            m_fft_found_peaks++;

            m_fft_highest_idx[i] = i_order_harmonic;
            m_fft_highest_pos[i] = m_fftfreqs[i_order_harmonic];
        }

        // Compute Total Harmonic Distortion
        // Source http://www.r-type.org/addtext/add183.htm
        if (m_fft_found_peaks)
        {
            m_thd = 0;
            double fundamental_db = current_fft_draw[m_fft_highest_idx[0]];
            double totdbc = 0;
            for (int i = 1; i < m_fft_found_peaks; ++i)
            {
                double dBc = current_fft_draw[m_fft_highest_idx[i]] - fundamental_db;
                totdbc += pow(10.0, dBc / 10.0);
            }

            m_thd = sqrt(totdbc) * 100.;
        }
    }

    void compute_channels_phase()
    {
        if (m_audiorecorder.get_channel_count() < 2) return;

        float fft_capture_size = m_capture_size / 2;

        // Get the fundamental frequency FFT result
        fftw_complex* right_comp = &(m_fftoutl[m_fft_highest_idx[m_fundamental_index]]);
        fftw_complex* left_comp = &(m_fftoutr[m_fft_highest_idx[m_fundamental_index]]);

        // Compute the phase (complex argument) of left and right channels
        double right_phase = wrap_phase(atan2((*right_comp)[1], (*right_comp)[0]));
        double left_phase = wrap_phase(atan2((*left_comp)[1], (*left_comp)[0]));
        // Compute the phase difference and convert to degrees
        m_phase_diff_degrees = wrap_phase(right_phase - left_phase) * 180. / M_PI;

        // Compute amplitude difference (diff of complex modules)
        double left_amplitude  = sqrt( ( (*left_comp)[0] * (*left_comp)[0] ) + ( (*left_comp)[1] * (*left_comp)[1]) ) / fft_capture_size;
        double right_amplitude = sqrt( ( (*right_comp)[0] * (*right_comp)[0] ) + ( (*right_comp)[1] * (*right_comp)[1]) ) / fft_capture_size;

        // Convert to dB
        m_left_right_db = 20. * log10(left_amplitude / right_amplitude);

        if (m_phase_time.size() < 200)
        {
            m_phase_history.push_back(m_phase_diff_degrees);
            m_lrdiff_history.push_back(m_left_right_db);
            m_phase_time.push_back(m_phase_time.size());
        }
        else 
        {
            memcpy(m_phase_history.data(), &m_phase_history[1], (m_phase_history.size() - 1) * sizeof(float));
            memcpy(m_lrdiff_history.data(), &m_lrdiff_history[1], (m_lrdiff_history.size() - 1) * sizeof(float));
            m_phase_history.back() = m_phase_diff_degrees;
            m_lrdiff_history.back() = m_left_right_db;
        }
    }

    void draw_tools_windows()
    {
        if (m_sound_setup_open && !m_sweep_started)
        {
            ImGui::SetNextWindowSize(ImVec2(600, 150));
            if(ImGui::Begin("Sound card setup", &m_sound_setup_open))
            {
                ImVec2 winsize = ImGui::GetWindowSize();
                ImGui::PushItemWidth(winsize.x / 3);
                const std::vector<std::string>& out_devices = m_audiomanager.get_output_devices();
                ImGui::SeparatorText("Output device");
                if (ImGui::Combo("Ouput", &m_combo_out, vector_getter, (void*)&out_devices, out_devices.size()))
                {
                    m_audio_out_idx = m_audiomanager.get_output_device_map(m_combo_out);
                    m_output_device = out_devices[m_combo_out];
                    reset_sine_generator();
                }
                ImGui::SameLine();
                const std::vector<std::string> out_samplerate = m_audio_out_idx >= 0 ? m_audiomanager.get_output_sample_rates_str(m_audio_out_idx) : std::vector<std::string>();
                if (ImGui::Combo("Samplerate##1", &m_out_sample_rate_idx, vector_getter, (void*)&out_samplerate, out_samplerate.size()))
                {
                    reset_sine_generator();
                }
                
                ImGui::SeparatorText("Input device");
                const std::vector<std::string>& in_devices = m_audiomanager.get_input_devices();
                if (ImGui::Combo("Input", &m_combo_in, vector_getter, (void*)&in_devices, in_devices.size()))
                {
                    m_audio_in_idx = m_audiomanager.get_input_device_map(m_combo_in);
                    m_input_device = in_devices[m_combo_in];
                    reinit_recorder();
                }
                ImGui::SameLine();
                const std::vector<std::string> in_samplerate = m_audio_in_idx >= 0 ? m_audiomanager.get_input_sample_rates_str(m_audio_in_idx) : std::vector<std::string>();
                if (ImGui::Combo("Samplerate##2", &m_in_sample_rate_idx, vector_getter, (void*)&in_samplerate, in_samplerate.size()))
                {
                    reinit_recorder();
                }
                ImGui::PopItemWidth();
            }
            ImGui::End();
        }
    }

    void save_soundcard_setup(std::map<std::string, std::string> &cnf)
    {
        const std::vector<std::string>& out_devices = m_audiomanager.get_output_devices();
        std::string out_device = out_devices[m_combo_out];
        const std::vector<std::string>& in_devices = m_audiomanager.get_input_devices();
        std::string in_device = in_devices[m_combo_in];

        cnf["output_device"] = out_device;
        cnf["input_device"] = in_device;
    }

    void get_configuration_string(std::map<std::string, std::string> &cnf) override
    {
        save_soundcard_setup(cnf);
    }

    void set_configuration_string(std::string s, std::string str) override
    {
        if (s == "input_device")
        {
            const std::vector<std::string>& in_devices = m_audiomanager.get_input_devices();
            std::vector<std::string>::const_iterator it = std::find(in_devices.begin(), in_devices.end(), str);
            if (it != in_devices.end())
            {
                m_combo_in = std::distance(in_devices.begin(), it);
                m_audio_in_idx = m_audiomanager.get_input_device_map(m_combo_in);
                printf("Restoring saved input dev found [%s] [%i]\n", str.c_str(), m_audio_in_idx);
                m_input_device = str;
            }
        }

        if (s == "output_device")
        {
            const std::vector<std::string>& out_devices = m_audiomanager.get_output_devices();
            std::vector<std::string>::const_iterator it = std::find(out_devices.begin(), out_devices.end(), str);
            if (it != out_devices.end())
            {
                m_combo_out = std::distance(out_devices.begin(), it);
                m_audio_out_idx = m_audiomanager.get_output_device_map(m_combo_out);
                printf("Restoring saved output dev found [%s] [%i]\n", str.c_str(), m_audio_out_idx);
                m_output_device = str;
            }
        }
    }

    void get_configuration_int(std::map<std::string, int> &cnf) override
    {
        cnf["logScaleFFT"]      = m_logscale_frequency == true ? 1 : 0;
        cnf["FFTwindowType"]    = m_fft_window_fn_index;
        cnf["showVoltmeter"]    = m_show_rms_voltage == true ? 1 : 0;
        cnf["theme"]            = m_uitheme;
        cnf["optimizedFFT"]     = m_optimized_fft == true ? 1 : 0;
        cnf["inSampleRateIdx"]  = m_in_sample_rate_idx;
        cnf["outSampleRateIdx"] = m_out_sample_rate_idx;
    }

    void set_configuration_int(std::string s, int i) override
    {
        if (s == "logScaleFFT")
            m_logscale_frequency = i;
        else if (s == "FFTwindowType")
        {
            m_fft_window_fn_index = i;
            set_window_fn();
        }
        else if (s == "showVoltmeter")
        {
            m_show_rms_voltage = i;
        }
        else if (s == "theme")
        {
            m_uitheme = i;
            set_theme();
        }
        else if (s == "optimizedFFT")
        {
            m_optimized_fft = i;
        } 
        else if (s == "inSampleRateIdx")
        {
            m_in_sample_rate_idx = i;
        }
        else if (s == "outSampleRateIdx")
        {
            m_out_sample_rate_idx = i;
        }
    }

    void get_configuration_float(std::map<std::string, float> &cnf) override
    {
        cnf["calibrationValue"] = m_rms_calibration_scale;
    }

    void set_configuration_float(std::string s, float f) override
    {
        if (s == "calibrationValue")
            m_rms_calibration_scale = f;
    }

    bool check_data_buffer()
    {
        int wanted_buffer = m_capture_size * m_audiorecorder.get_channel_count();
        if (m_audiorecorder.get_available_samples() >= wanted_buffer)
        {
            bool computed = compute();
            if (computed)
            {
                if (m_show_thd) compute_thd();
                if (m_show_thd) compute_thdn();
                if (m_compute_channel_phase) compute_channels_phase();
            }

            if (computed) update_ui();
                return true;
        }

        return false;
    }
};

class MainWindow : public Window_SDL
{   
    AudioToolWindow* m_audiotool;
public:
    MainWindow() : Window_SDL("TapeTools", 1200, 900)
    {
        size_t font_data_size = _font_blob_end - _font_blob_start;
        load_font_from_memory((const char*)_font_blob_start, font_data_size, 16);
        m_audiotool = new AudioToolWindow(this);
    }

    bool probe_event() override
    {
        if(m_audiotool->check_data_buffer())
        {
            return true;
        }
        return false;
    }

    virtual ~MainWindow()
    {

    }

    void draw(bool c) override
    {
        Window_SDL::draw(c);
    }
};

int main(int argc, char *argv[])
{
    App_SDL *app = App_SDL::get();
    app->set_app_name("TapeTools");
    Window_SDL *window = new MainWindow;

    window->set_minimum_window_size(1400, 1000);

    app->add_window(window);
    app->run();

    return 0;
}