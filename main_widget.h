
#pragma once

#include <SDL.h>
#include "imgui_internal.h"
#include "window_sdl.h"
#include "audio_generator.h"
#include "audio_recorder.h"
#include "audio_loopback.h"
#include "utils.h"
#include "timer.h"
#include <fftw3.h>
#include <algorithm>
#include <stdarg.h>
#include <Dsp.h>
#include "Hack-Regular.h"
#include "sdr_thread.h"

const double WOW_FLUTTER_ANALYSIS_TIME = 5.5;
const int    WOW_FLUTTER_DECIMATION = 20;


class AudioToolWindow : public Widget
{
    friend class WowAndFluterThread;

    PAaudioManager      m_audiomanager;
    PAaudioRecorder     m_audiorecorder;
    PAaudioLoopback     m_audioloopback;
    PAaudioWaveformGenerator  m_signal_generator;

    int  m_uitheme = 0;
    
    bool m_signal_generator_switch = false;
    int  m_signal_generator_pitch = 1000;
    float m_signalgen_latency_s = 0.01f;
    int m_recorder_latency_ms = 100;
    int m_signalgen_volume_db = 0;
    int m_output_hw_volume_db = 0;
    int m_input_gain = 0;

    float m_input_volume_min = -60.0f;
    float m_input_volume_max = 0.0f;
    
    int m_audio_out_idx = -1;
    int m_audio_in_idx = -1;
    int m_audio_loopback_out_idx = -1;

    std::string m_input_device;
    std::string m_output_device;
    std::string m_output_loopback_device;

    std::vector<double> m_sound_data1, m_sound_data2;
    std::vector<double> m_longterm_audio;
    std::vector<double> m_wow_flutter_data, m_wow_flutter_data_x;
    std::vector<double> m_sound_data_x;
    unsigned long m_wf_compute_time = 0;
    // Wow flutter IQ data
    std::vector<double> m_signal_i;
    std::vector<double> m_signal_q;
    std::vector<float> m_raw_buffer;
    fftw_plan m_fftplanr = NULL;
    fftw_plan m_fftplanl = NULL;
    fftw_plan m_fftplanwow = NULL;
    double *m_fftinl = nullptr;
    fftw_complex *m_fftoutl = nullptr;
    double *m_fftinr = nullptr;
    fftw_complex *m_fftoutr = nullptr;
    fftw_complex *m_wow_complex_out = nullptr;
    double *m_fft_modules = nullptr;
    double *m_fftdrawl = nullptr;
    double *m_fftdrawr = nullptr;
    double *m_fftfreqs = nullptr;
    std::vector<double> m_fftwowdrawfreqs;
    std::vector<double> m_fftdrawwow;
    int m_capture_size = 0;
    double m_audio_gain = 1.0f;
    int m_combo_in = 0;
    int m_combo_out = 0;
    int m_combo_out_loopback = 0;
    int m_in_sample_rate_idx = 0;
    int m_out_sample_rate_idx = 0;
    int m_wow_flutter_capture_size = 0;
    bool m_show_wf_fft_view = false;

    bool m_sound_setup_open = false;
    bool m_compute_channel_phase = false;
    bool m_logscale_frequency = true;
    bool m_show_wow_flutter = false;
    bool m_show0db = false;
    double m_rms_calibration_scale = 1.0f;
    float m_scopezoom = 1;;
    std::vector<std::string> m_wmodes = {"Rectangle", "Hamming", "Hann-Poisson", "Blackman", "Blackman-Harris", "Hann", "Kaiser 6"};
    std::vector<std::string> m_fftchannels = {"Left", "Right"};
    double m_window_amplitude_correction[8] = {0.0};
    double m_window_energy_correction[8] = {0.0};

    double  (*m_window_fn)(int, int) = hann_fft_window;
    int     m_fft_window_fn_index = 5;
    double  *m_current_window_cache = nullptr;
    bool    m_fft_channel_left = true;
    bool    m_fft_channel_right = false;
    double  m_noise_foor = -100;
    double  m_fft_harmonics_freq[20] = {0};
    int     m_fft_harmonics_idx[20] = {0};
    double  m_fft_highest_val;
    int     m_fft_found_peaks = 0;
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
    int     m_sweep_target_frequency;
    int     m_sweep_capture_num = 30;
    int     m_measure_delay = 400;
    int     m_sweep_last_measure_freq;
    float   m_sweep_threshold_level = -50;
    std::vector<double> m_sweep_values;
    std::vector<double> m_sweep_freqs;

    std::vector<std::pair< std::vector<double>, std::vector<double>> > m_mem_sweeps_results;
    std::vector<std::string> m_mem_sweeps_names;

    Chrono  m_sweep_timer_chrono;
    
    int     m_sweep_time = 0;
    bool    m_sweep_status = false;
    bool    m_compute_on = false;
    bool    m_audio_loopback_on = false;

    bool    m_use_targetdb = false;
    bool    m_lockdb = false;
    float   m_target_db = 0.0;
    double  m_locked_db_value = 0.0;
    int     m_current_db_target_channel = 0;

    bool    m_optimized_fft = false;

    int     m_wow_test_frequency = 1;
    int     m_wow_test_frequency_custom = 3000;
    float   m_wow_peak_detection = 0;
    int     m_wf_filter_freq_combo = 0;
    float   m_wow_mean = 0;

    bool    m_trigger_on = true;
    int     m_trigger_index = 0;

    bool    m_debug_info = false;
    bool    m_show_log_window = false;

    bool    m_wasapi_exclusive = false;
    bool    m_use_floatingpoint = true;
    bool    m_wasapi_polling = true;

    std::vector<std::string> m_debug_logs;
    unsigned long m_total_compute_time=0;
    unsigned long m_ui_time=0;
    ThreadMutex m_wow_data_mutex;
#ifdef RTL_SDR
    SdrThread m_sdr_thread;
 #endif
    DECLARE_METHODS(on_device_changed)
    DECLARE_METHODS(on_backend_disconnected)

    void set_sound_config();
    void reset_audiomanager();
    void detect_periods();

    void process_sweep();

public:
    AudioToolWindow(Window_SDL* win);
    virtual ~AudioToolWindow();

    void destroy_capture();
    void init_capture();
    void reinit_recorder();
    void reset_signal_generator();

    void set_theme();

    void check_settings_loaded();

    void draw() override;

    void start_sweep_gen();
    void stop_sweep_gen();

    void set_window_fn(bool compute_cache = true);
    bool compute();
    void compute_wow_and_flutter();
    void compute_thdn();
    void compute_thd();
    void compute_channels_phase();
    void compute_fft_window_cache();
    void compute_fft_window_corrections(int num_samples = 1000);

    void draw_sweep_tab();
    void draw_sdr();
    void draw_lcd(const float value, const ImVec2 size, const int lcd_digits_size);
    void draw_rt_analysis_tab();

    void draw_audio_time_domain_widget(int plotheight, int current_sample_rate, int channelcount);
    void draw_wow_flutter_widget(int channelcount, int current_samplerate, int plotheight);
    void draw_voltmeter_widget(int channel_count);
    void draw_audio_fft_widget(int channelcount, int current_sample_rate, int plotheight);
    void draw_channels_phase_widget(int plotheight);
    void draw_tone_generator_widget();
    void draw_input_control_widget();

    void draw_tools_windows();
    void draw_log_window();
    void save_soundcard_setup(std::map<std::string, std::string> &cnf);
    void get_configuration_string(std::map<std::string, std::string> &cnf) override;
    void set_configuration_string(std::string s, std::string str) override;
    void get_configuration_int(std::map<std::string, int> &cnf) override;
    void set_configuration_int(std::string s, int i) override;
    void get_configuration_float(std::map<std::string, float> &cnf) override;
    void set_configuration_float(std::string s, float f) override;
    bool check_data_buffer();

    void log_message(std::string msg);
};