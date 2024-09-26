
#include <SDL.h>
#include "imgui_internal.h"
#include "window_sdl.h"
#include "audio_sine_gen.h"
#include "audio_record.h"
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
    audioManager        m_audiomanager;
    audioSineGenerator  m_sine_generator;
    audioRecorder       m_audiorecorder;

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
    double *m_fftfreqs = nullptr;
    std::vector<double> m_fftwowdrawfreqs;
    std::vector<double> m_fftdrawwow;
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
        //reinit_recorder();
        printf("Audio device configuration changed.\n");
    }

    CALLBACK_METHOD(on_backend_disconnected, AudioToolWindow)
    {
        reset_audiomanager();
    }

    void set_sound_config()
    {
        if (m_input_device.empty()) m_audio_in_idx  = m_audiomanager.get_default_input_device_id();
        if (m_output_device.empty()) m_audio_out_idx = m_audiomanager.get_default_output_device_id();

        m_combo_in  = m_audiomanager.get_input_device_reverse_map(m_audio_in_idx);
        m_combo_out = m_audiomanager.get_output_device_reverse_map(m_audio_out_idx);
    }

    void reset_audiomanager()
    {
        set_sound_config();
        reinit_recorder();
        reset_sine_generator();
    }

    void detect_periods();

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
    }

    virtual ~AudioToolWindow()
    {
        m_sine_generator.destroy();
        m_sdr_thread.stop();
        m_sdr_thread.join();
        destroy_capture();
    }

    void destroy_capture();
    void init_capture();
    void reinit_recorder();
    void reset_sine_generator();

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
        if (!settings_loaded)
        {
            if (GImGui->SettingsLoaded) printf("Settings restored\n");
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
        if (m_sdr_thread.get_scanner().get_rtl_device().get_device_count() && ImGui::BeginTabItem("SDR analysis"))
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

    void set_window_fn(bool compute_cache = true);
    bool compute(bool compute_fft = true, bool compute_noise_floor = true);
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
                if (m_compute_channel_phase || m_show_thd) compute_thd();
                if (m_show_thd) compute_thdn();
                if (m_compute_channel_phase) compute_channels_phase();
            }

            if (computed) update_ui();
            return true;
        }

        if (m_sdr_thread.data_available())
        {
            update_ui();
            return true;
        }

        return false;
    }
};