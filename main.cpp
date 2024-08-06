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

extern "C" {
    extern unsigned char _font_blob_end[];
    extern unsigned char _font_blob_start[];
}


class MainWindow2 : public Window_SDL
{
    class Test : public Widget {
        public:
        Test(Window_SDL *win) : Widget(win, "Test")
        {
            
        }
        ~Test(){

        }

        void draw() override {
            ImGui::ShowDemoWindow();
        }
    };

    Test* test;
    public:
    MainWindow2() : Window_SDL("Test2", 1200, 900)
    {
        test = new Test(this);
        set_lazy_mode(false);
    }

    virtual ~MainWindow2()
    {
    }

    void draw(bool c) override
    {
        Window_SDL::draw(c);
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
    
    std::vector<double> m_sound_data1, m_sound_data2;
    std::vector<double> m_sound_data_x;
    std::vector<double> m_raw_buffer;
    fftw_plan m_fftplan = NULL;
    double *m_fftin = nullptr;
    fftw_complex *m_fftout = nullptr;
    double *m_rms_fft = nullptr;
    double *m_fftdraw = nullptr;
    double *m_fftfreqs = nullptr;
    double *m_fftfiltered = nullptr;
    int m_capture_size = 0;
    double m_audio_gain = 1.0f;
    int m_combo_in = 0;
    int m_combo_out = 0;
    int m_in_sample_rate = 0;
    int m_out_sample_rate = 0;

    bool m_sound_setup_open = false;
    bool m_compute_thd = false;
    bool m_logscale_frequency = true;
    bool m_show_xy = false;
    bool m_show0db = false;
    double m_rms_calibration_scale = 1.0f;
    float m_scopezoom = 1;;
    std::vector<std::string> m_wmodes = {"Rectangle", "Hamming", "Hann-Poisson", "Blackman", "Blackman-Harris", "Hann", "Kaiser 5", "Kaiser 7"};
    double m_window_amplitude_correction[8] = {0.0};
    double m_window_energy_correction[8] = {0.0};
    std::vector<std::string> m_fftchannels = {"Left", "Right"};

    double  (*m_window_fn)(int, int) = hann_fft_window;
    int     m_fft_window_fn_index = 5;
    double  *m_current_window_cache = nullptr;
    int     m_fft_channel = 0;
    double  m_noise_foor = -100;
    double  m_fft_highest_pos[200];
    int     m_fft_highest_idx[200];
    double  m_fft_highest_val;
    int     m_fft_found_peaks = 0;
    int     m_fundamental_index = 0;
    int     m_fft_fund_idx_range_min = 0;
    int     m_fft_fund_idx_range_max = 0;
    bool    m_smooth_fft = true;
    double  m_thd = 0;
    double  m_thdn = 0;
    double  m_thddb = 0;

    double  m_rms_left = 0, m_rms_right = 0;
    bool    m_show_rms_voltage = false;

    bool    m_sweep_started = false;
    bool    m_async_sweep = false;
    int     m_sweep_current_frequency;
    int     m_sweep_span = 250;
    int     m_measure_delay = 400;
    std::vector<double> m_sweep_values;
    std::vector<double> m_sweep_freqs;
    Timer   m_sweep_timer;
    bool    m_pause_compute = false;

    bool    m_use_targetdb = false;
    bool    m_lockdb = false;
    float   m_target_db = 0.0;
    double  m_locked_db_value = 0.0;
    int     m_current_db_target_channel = 0;

    int     m_zscore_lag = 40;
    float   m_zscore_influence = 0.5;
    float   m_zscore_threshold = 3.5;
    bool    m_show_zscore_settings = false;
    
    CALLBACK_METHOD(on_timer_event, AudioToolWindow)
    {
        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        float fft_step = m_capture_size / current_sample_rate;
        if (!m_async_sweep){
            
            if (m_sweep_current_frequency >= 20000){
                // We reached the end of the measure
                stop_sweep_gen();
                return;
            }

            int min_freq_idx = std::max(int((m_sweep_current_frequency-500)*fft_step), 0);
            int max_freq_idx = std::min(int((m_sweep_current_frequency+500)*fft_step), m_capture_size / 2);

            double max_val = m_noise_foor;
            for (int i = min_freq_idx; i < max_freq_idx; ++i){
                if (m_fftdraw[i] > max_val){
                    max_val = m_fftdraw[i];
                }
            }

            m_sweep_values.push_back(max_val);
            m_sweep_freqs.push_back(m_sweep_current_frequency);


            m_sweep_current_frequency += m_sweep_span;
            m_sine_generator.set_pitch(m_sweep_current_frequency);
        } else {
            double max_val = m_noise_foor;
            double frequency = -1;
            for (int i = 0; i < m_capture_size / 2; ++i){
                if (m_fftdraw[i] > max_val){
                    max_val = m_fftdraw[i];
                    frequency = double(i) / fft_step;
                }
            }
            if (frequency < 0){
                return;
            }
            int index = 0;
            bool found = false;
            for (auto freq : m_sweep_freqs){
                if (freq == frequency){
                    if (m_sweep_values[index] < max_val)
                    m_sweep_values[index] = max_val;
                    found = true;
                    break;
                }
                if (freq > frequency && index > -1){
                    m_sweep_values.insert(m_sweep_values.begin() + index, max_val);
                    m_sweep_freqs.insert(m_sweep_freqs.begin() + index, frequency);
                    found = true;
                    break;
                }
                index++;
            }
            if (!found){
                m_sweep_values.push_back(max_val);
                m_sweep_freqs.push_back(frequency);
            }
        }
        m_sweep_timer.start();
        update_ui();
    }

    CALLBACK_METHOD(on_recorder_data_ready, AudioToolWindow)
    {
        if (m_pause_compute){
            return;
        }
        bool computed = compute();
        if (computed && m_compute_thd)
        {
            compute_thd();
            compute_thdn();
        }
        if (computed) update_ui();
    }

    CALLBACK_METHOD(on_device_changed, AudioToolWindow)
    {
        //reset_audiomanager();
        printf("Device changed\n");
    }

    CALLBACK_METHOD(on_backend_disconnected, AudioToolWindow)
    {
        reset_audiomanager();
    }

    void reset_audiomanager()
    {
        m_audio_out_idx = m_audiomanager.get_default_output_device_id();
        m_audio_in_idx  = m_audiomanager.get_default_input_device_id();

        m_combo_in  = m_audiomanager.get_input_device_reverse_map(m_audio_in_idx);
        m_combo_out = m_audiomanager.get_output_device_reverse_map(m_audio_out_idx);
        reinit_recorder();
        reset_sine_generator();
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

        m_audiomanager.device_changed_event.connect_event(STATIC_METHOD(on_device_changed), this);
        m_audiomanager.backend_disconnected_event.connect_event(STATIC_METHOD(on_backend_disconnected), this);
        m_audiorecorder.buffer_full_event.connect_event(STATIC_METHOD(on_recorder_data_ready), this);

        m_audiomanager.flush();
        m_sweep_timer.connect_event(STATIC_METHOD(on_timer_event), this);
    }

    virtual ~AudioToolWindow()
    {
        m_sine_generator.destroy();
        destroy_capture();
    }

    void destroy_capture()
    {
        if (m_fftplan) fftw_destroy_plan(m_fftplan);

        delete[] m_fftin;
        delete[] m_fftout;
        delete[] m_fftdraw;
        delete[] m_fftfreqs;
        delete[] m_fftfiltered;
        delete[] m_rms_fft;
        delete[] m_current_window_cache;
        m_sound_data_x.clear();

        m_fftin = nullptr;
        m_fftout = nullptr;
        m_fftdraw = nullptr;
        m_fftfreqs = nullptr;
        m_fftfiltered = nullptr;
        m_fftplan = nullptr;
        m_current_window_cache = nullptr;
    }

    void init_capture()
    {
        int capture_size = m_audiorecorder.get_buffer_size(float(m_recorder_latency_ms) / 1000.f, false);
        if (capture_size == 0){
            return;
        }
        destroy_capture(); 
        m_capture_size = capture_size;
        m_fftin = new double[capture_size];
        m_fftout = new fftw_complex[capture_size];
        m_fftdraw = new double[capture_size/2];
        m_fftfreqs = new double[capture_size/2];   
        m_fftfiltered = new double[capture_size/2];   
        m_rms_fft = new double[capture_size/2];
        m_current_window_cache = new double[capture_size];
        m_fftplan = fftw_plan_dft_r2c_1d(capture_size, m_fftin, m_fftout, FFTW_MEASURE | FFTW_PRESERVE_INPUT);
        m_fft_channel = 0;
        compute_fft_window_cache();
    }

    void reinit_recorder()
    {
        if (m_audio_in_idx < 0){
            return;
        }

        if (m_audiorecorder.init(float(m_recorder_latency_ms) / 1000.f, m_audio_in_idx, m_audiomanager.get_input_sample_rates(m_audio_in_idx)[m_in_sample_rate]))
        {
            m_audiorecorder.start();
        }
        init_capture();
    }

    void reset_sine_generator()
    {
        int current_sine_samplerate = m_audiomanager.get_output_sample_rates(m_audio_out_idx)[m_out_sample_rate];
        m_sine_generator.destroy();
        m_sine_generator.init(m_audiomanager, m_audio_out_idx, current_sine_samplerate, m_sinegen_latency_s);
        m_sine_generator.set_pitch(m_pitch);
        m_sine_generator.start();
        m_sine_generator.pause(!m_sine_generator_switch);
    }

    void compute_fft_window_cache(){
        if (m_current_window_cache == nullptr){
            delete m_current_window_cache;
        }
        m_current_window_cache = new double[m_capture_size];
        for(int i = 0; i < m_capture_size; ++i){
            m_current_window_cache[i] = m_window_fn(i, m_capture_size);
        }
    }

    void compute_fft_window_corrections(){
        int tmp = m_fft_window_fn_index;
        for (int j = 0; j < 8; ++j){
            m_fft_window_fn_index = j;
            set_window_fn(false);
            double sum = 0;
            double rms = 0;
            for (int i = 0; i < 1000; i++){
                double val = m_window_fn(i, 1000);
                sum += val;
                rms += val*val;
            }

            // Normalization
            m_window_amplitude_correction[j] = 1.0 / (sum * 0.001);
            m_window_energy_correction[j] = 1.0 / sqrt(rms * 0.001);
        }
        // Restore
        m_fft_window_fn_index = tmp;
    }

    void set_theme()
    {
        if (m_uitheme == 0){
            ImGui::StyleColorsDark();   
        }
        if (m_uitheme == 1){
            ImGui::StyleColorsLight();   
        }
        if (m_uitheme == 2){
            ImGui::StyleColorsClassic();   
        }
        ImGui::GetStyle().FrameRounding = 5.0;
        ImGui::GetStyle().ChildRounding = 5.0;
        ImGui::GetStyle().WindowRounding = 4.0;
        ImGui::GetStyle().GrabRounding = 4.0;
        ImGui::GetStyle().GrabMinSize = 4.0;
    }

    void draw() override 
    {
        m_audiomanager.flush();
        int channelcount = m_audiorecorder.get_channel_count();
    
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Preferences")){
                if (ImGui::BeginMenu("Theme")){
                    if (ImGui::MenuItem("Dark", nullptr, nullptr)){
                        m_uitheme = 0;
                        set_theme();
                    }
                    if (ImGui::MenuItem("Light", nullptr, nullptr)){
                        m_uitheme = 1;
                        set_theme();
                    }
                    if (ImGui::MenuItem("Classic", nullptr, nullptr)){
                        m_uitheme = 2;
                        set_theme();
                    }
                    ImGui::EndMenu();
                }
                ImGui::MenuItem("Sound card setup", nullptr, &m_sound_setup_open);
                if(ImGui::MenuItem("Show Zscore settings", nullptr, nullptr)){
                    m_show_zscore_settings = !m_show_zscore_settings;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::BeginTabBar("MaintabBar");
        if (ImGui::BeginTabItem("Realtime analysis")){
            draw_rt_analysis_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sweep measurement")){
            draw_sweep_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();

        draw_tools_windows();
    }

    void start_sweep_gen()
    {
        m_sweep_started = true;
        m_sweep_current_frequency = 20;
        m_sweep_freqs.clear();
        m_sweep_values.clear();
        m_sine_generator_switch = true;
        reset_sine_generator();
        m_sine_generator.set_pitch(m_sweep_current_frequency);
        m_sweep_timer.start();
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
        switch (m_fft_window_fn_index){
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
        if (compute_cache){
            compute_fft_window_cache();
        }
    }

    void draw_sweep_tab()
    {
        int channelcount = m_audiorecorder.get_channel_count(); 
        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        float frameh = ImGui::GetFrameHeightWithSpacing();
        float padh = 3.0f * ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ItemSpacing.y;

        ImGui::BeginChild("ScopesChild1", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);

        ImGui::BeginChild("ScopesChild2", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (!m_sweep_started){
            ImGui::ToggleButton("Async", &m_async_sweep);
            ImGui::SameLine();
            if (ImGui::Button("Start")){
                start_sweep_gen();
            }
        } else {
            if (ImGui::Button("Stop")){
                stop_sweep_gen();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("FFTChildToneVol", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(70);
        if (ImGui::SliderInt("Tone power", &m_sine_volume_db, -100, 0, "%d dB")){
            m_sine_generator.set_volume(m_sine_volume_db);
        }
        ImGui::EndChild();
        
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild3", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Log scale frequency", &m_logscale_frequency);
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("ScopesChild4", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(70);
        if(ImGui::DragInt("Delay (ms)", &m_measure_delay, 1.f, m_recorder_latency_ms * 2, 3000)){
            // Set a comfortable time amount to let the FFT settle (at leat 400ms for 200ms latency)
            m_sweep_timer.set(m_measure_delay);
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildSpan", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(70);
        ImGui::DragInt("Sweep span (Hz)", &m_sweep_span, 10.f, 50, 2000);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild5", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Window mode", &m_fft_window_fn_index, vector_getter, (void *)&m_wmodes, m_wmodes.size()))
        {
            set_window_fn();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild6", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("Channel", &m_fft_channel, vector_getter, (void *)&m_fftchannels, m_fftchannels.size());
        ImGui::EndChild();

        if (ImPlot::BeginPlot("AudioFFT", ImVec2(-1, -1))){
            float xfftmax = current_sample_rate > 0 ? (current_sample_rate)/2.f : INFINITY;
            ImPlot::SetupAxes("Frequency", "dB FullScale", 0, ImPlotAxisFlags_Lock);
            if (m_logscale_frequency){
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
            }
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
            ImPlot::SetupAxesLimits(20.f, xfftmax, -130.0, 0.0);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 20.0, 20000.0);

            if (channelcount>0 && m_fftfreqs)
            {
                ImPlot::PlotLine("Audio FFT", m_fftfreqs, m_fftdraw, m_sound_data_x.size()/2);
                double nf[4] = {0., (current_sample_rate)/2.0, m_noise_foor, m_noise_foor};
                ImPlot::PlotLine("Noise floor", nf, nf+2, 2);
                ImPlot::PlotLine("Frequency response", m_sweep_freqs.data(), m_sweep_values.data(), m_sweep_freqs.size());
            }
            
            ImPlot::EndPlot();
        }
        ImGui::EndChild();
    }

    void draw_lcd(float value, ImVec2 size)
    {
        char voltmeter[10];
        snprintf(voltmeter, 10, "%.4f", value);

        ImGui::InvisibleButton("canvas", size);
        ImVec2 p0 = ImGui::GetItemRectMin();
        ImVec2 p1 = ImGui::GetItemRectMax();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->PushClipRect(p0, p1);

        int textsize = strlen(voltmeter);
        const int lcd_digits_size = 6;
        float p=0.02*size.x,s=size.x/lcd_digits_size-p,x=s*.5,y=size.y*.5;
        for(int i=(textsize-1) - (textsize - lcd_digits_size);i>=0;i--){
            if (voltmeter[i] >= '0' && voltmeter[i] <= '9'){
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
        if (!m_sweep_started){
            ImGui::BeginChild("ScopesChildToneGen", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);

            ImGui::BeginChild("ScopesChildTonGenSwitch", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            if(ImGui::ToggleButton("Tone generator", &m_sine_generator_switch)){
                reset_sine_generator();
            }
            ImGui::SetItemTooltip("Sine generator ON/OFF");

            ImGui::SameLine();
            if (ImGui::SliderInt("Pitch", &m_pitch, 20, 20000)){
                m_sine_generator.set_pitch(m_pitch);
            }
            ImGui::SetItemTooltip("Set the pitch of the sine generator");
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("ScopesChildToneVol", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            if (ImGui::SliderInt("Intensity", &m_sine_volume_db, -100, 0, "%d dB")){
                m_sine_generator.set_volume(m_sine_volume_db);
            }
            ImGui::SetItemTooltip("Set the generator intensity");
            ImGui::EndChild();

            ImGui::SameLine();
            if(ImGui::Button("test")){
                MainWindow2* win = new MainWindow2;
                App_SDL::get()->add_window(win);
            }

            ImGui::EndChild();
        }


        /*
        * Time domain analysis
        */
        ImGui::BeginChild("ScopesChild", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
        float plotheight = height() / 2.0f - 5.f;


        ImGui::BeginChild("ScopesChild1", ImVec2(0, plotheight), ImGuiChildFlags_Border, ImGuiWindowFlags_None);

        if (channelcount > 1){
            ImGui::BeginChild("ScopesChildShowXY", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            ImGui::ToggleButton("XY diagram", &m_show_xy);
            ImGui::SetItemTooltip("Shows the XY phase diagram panel");
            ImGui::EndChild();
            ImGui::SameLine();
        }

        ImGui::BeginChild("ScopesChildShowRmsVolts", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Show voltmeter", &m_show_rms_voltage);
        ImGui::SetItemTooltip("Shows the voltmeters panel");
        ImGui::EndChild();
        ImGui::SameLine();
       
        ImGui::BeginChild("ScopesChildYzoom", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(70);
        ImGui::SliderFloat("Amplitude mult", &m_scopezoom, 1, 50);
        ImGui::SetItemTooltip("Zoom Y axis");
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildCalib", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        static double rms_calibration = 1.0;
        if (m_rms_calibration_scale == 1.0){
            ImGui::SetNextItemWidth(70);
            ImGui::InputDouble("Measured RMS", &rms_calibration);
            ImGui::SetItemTooltip("Enter the measured RMS voltage here to calibrate the meters/graph");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            if (ImGui::Button("Calibrate from left")){

                m_rms_calibration_scale = rms_calibration / m_rms_left;
            }
            ImGui::SetItemTooltip("Do the calibration from left channel");
            if (channelcount > 1){
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                if (ImGui::Button("Calibrate from right")){
                    m_rms_calibration_scale = rms_calibration / m_rms_right;
                }
                ImGui::SetItemTooltip("Do the calibration from right channel");
            }
        }
        if (m_rms_calibration_scale != 1.0){
            ImGui::SameLine();
            if (ImGui::Button("Clear calibration")){

                m_rms_calibration_scale = 1.0;
                rms_calibration = 1.0;
            }
            ImGui::SetItemTooltip("Clear the calibration");
        }
        ImGui::EndChild();

        /*
        *   0dBm Ref section 
        */

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildShow0db", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Show 0dBm Ref", &m_show0db);
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
        if (m_use_targetdb){
            if (!m_lockdb){
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60);
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

        if (m_show_rms_voltage){
            if (m_rms_calibration_scale == 1.0){
                lcd_fg = IM_COL32(200,0,0,255);
            } else {
                lcd_fg = IM_COL32(0,200,0,255);
            }
            ImGui::BeginChild("ScopesChildVoltageLcd", ImVec2(0, -1), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeX  | ImGuiWindowFlags_None);
             
            ImGui::BeginChild("ScopesChildVoltageLcd1", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Volts RMS Left");
            draw_lcd(m_rms_left * m_rms_calibration_scale, ImVec2(200, 70));
            if (m_lockdb && m_current_db_target_channel == 0){
                float target_val_left = 1.f - fabs( m_locked_db_value - m_rms_left * m_rms_calibration_scale ) * 10.f;
                ImGui::ProgressBar(target_val_left);
            }
            ImGui::EndChild();

            //ImGui::SetCursorPosY(height());
            ImGui::BeginChild("ScopesChildVoltageLcd2", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Volts RMS Right");
            draw_lcd(m_rms_right * m_rms_calibration_scale, ImVec2(200, 70));
            if (m_lockdb && m_current_db_target_channel == 1){
                float target_val_right = 1.f - fabs( m_locked_db_value - m_rms_right * m_rms_calibration_scale ) *10.f;
                ImGui::ProgressBar(target_val_right);
            }
            ImGui::EndChild();
            ImGui::EndChild();
            ImGui::SameLine();
        }

        if (ImPlot::BeginPlot("Audio", ImVec2(m_show_xy ? width()-plotheight-10 : -1, -1)))
        {
            double x_limit = 1.0f / m_scopezoom;
            double xmax = current_sample_rate > 0 ? float(m_capture_size) * (1.0 / (current_sample_rate * 0.001)) : INFINITY;
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, xmax);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -x_limit, x_limit, ImPlotCond_Always);

            if (m_rms_calibration_scale != 1.0){
                ImPlot::SetupAxis(ImAxis_Y2, "Volts", ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines);
                ImPlot::SetupAxisLimits(ImAxis_Y2, -m_rms_calibration_scale / m_scopezoom, m_rms_calibration_scale / m_scopezoom, ImPlotCond_Always);
            }
            ImPlot::SetupAxes("Time (ms)", "Amplitude", 0, ImPlotAxisFlags_Lock);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, xmax);
            
            if (channelcount > 0){
                ImPlot::PlotLine("Left channel", m_sound_data_x.data(), m_sound_data1.data(), m_sound_data_x.size());
            }
            if (channelcount > 1){
                ImPlot::PlotLine("Right channel", m_sound_data_x.data(), m_sound_data2.data(), m_sound_data_x.size());
            }
            
            char rmstext[20];
            ImVec2 plotpos = ImPlot::GetPlotPos();
            ImVec2 plotsize = ImPlot::GetPlotSize();

            if (channelcount > 0){
                double rms[4] = {0., (current_sample_rate)/2.0, m_rms_left, m_rms_left};
                ImPlot::PlotLine("signal RMS left", rms, rms+2, 2);
            }
            if (channelcount > 1){
                double rms[4] = {0., (current_sample_rate)/2.0, m_rms_right, m_rms_right};
                ImPlot::PlotLine("signal RMS right", rms, rms+2, 2);
            }

            if(m_use_targetdb){
                if (!m_lockdb){
                    m_locked_db_value = ((m_current_db_target_channel == 0) ? m_rms_left : m_rms_right) * pow(10, m_target_db/20.0);
                }
                double tgtpnt[4] = {0., (current_sample_rate)/2.0, m_locked_db_value, m_locked_db_value};
                ImPlot::PlotLine("target dB", tgtpnt, tgtpnt+2, 2);
            }

            if (m_show0db){
                double zerodb = .775f / m_rms_calibration_scale;
                double rms[4] = {0., (current_sample_rate)/2.0, zerodb, zerodb};
                ImPlot::PlotLine("0 dB Reference", rms, rms+2, 2);
            }

            ImPlot::EndPlot();
        }
        
        ImGui::SameLine();
        if (m_show_xy && ImPlot::BeginPlot("X-Y Diagram", ImVec2(plotheight, -1)))
        {
            ImPlot::SetupAxes("X", "Y", ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock);
            float limit = 1.f / m_scopezoom;
            ImPlot::SetupAxesLimits(-limit, limit, -limit, limit, ImPlotCond_Always);
            if (channelcount > 1){
                ImPlot::PlotLine("Channels phase", m_sound_data1.data(), m_sound_data2.data(), m_sound_data_x.size());
            }
            ImPlot::EndPlot();
        }

        ImGui::EndChild();

        /*
        * Frequency domain analysis
        */
        ImGui::BeginChild("fftscopechild", ImVec2(0, -1), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
        ImGui::BeginChild("ScopesChild3", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);

        ImGui::BeginChild("ScopesChild4", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Log scale frequency", &m_logscale_frequency);
        ImGui::SetItemTooltip("Log scale/linear scale X axis");
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildSmoothFFT", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Smooth FFT", &m_smooth_fft);
        ImGui::SetItemTooltip("Smooth the FFT (Do not use when computing THD)");
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild5", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Compute THD", &m_compute_thd);
        ImGui::SetItemTooltip("Enable THD measurement");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild6", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Window mode", &m_fft_window_fn_index, vector_getter, (void *)&m_wmodes, m_wmodes.size()))
        {
            set_window_fn();
        }
        ImGui::SetItemTooltip("Set the FFT windowing mode");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild7", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("Channel", &m_fft_channel, vector_getter, (void *)&m_fftchannels, m_fftchannels.size());
        ImGui::SetItemTooltip("Which audio channel to analyse");
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild8", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(100);
        if(ImGui::InputInt("Capture size (ms)", &m_recorder_latency_ms, 50, 200, ImGuiInputTextFlags_EnterReturnsTrue)){
            if(m_recorder_latency_ms < 50) m_recorder_latency_ms = 50;
            if(m_recorder_latency_ms > 1000) m_recorder_latency_ms = 1000;
            must_reinit_recorder = true;
        }
        ImGui::SetItemTooltip("Audio sampling time in millisecond");
        ImGui::EndChild();

        if (m_show_zscore_settings && m_compute_thd){
            ImGui::SameLine();
            ImGui::BeginChild("ScopesChildZScore", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            ImGui::SetNextItemWidth(50);
            ImGui::SliderInt("ZscoreLag", &m_zscore_lag, 5, 500);
            ImGui::SetItemTooltip("Set the length of the Z-score algorithm (for peak detection)");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            ImGui::SliderFloat("ZscoreInfl.", &m_zscore_influence, 0., 1.);
            ImGui::SetItemTooltip("Influence of Z-score");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            ImGui::SliderFloat("ZscoreThres.", &m_zscore_threshold, 0.5, 100.);
            ImGui::SetItemTooltip("Threshold of Z-score");
            ImGui::EndChild();
        }

        ImGui::EndChild();

        if (ImPlot::BeginPlot("AudioFFT", ImVec2(-1, -1))){
            float xfftmax = current_sample_rate > 0 ? (current_sample_rate)/2.f : INFINITY;
            ImPlot::SetupAxes("Frequency", "dB FullScale", 0, ImPlotAxisFlags_Lock);
            if (m_logscale_frequency){
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
            }
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
            ImPlot::SetupAxesLimits(20.f, xfftmax, -120.0, 20.0);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 20.f, 20000.f);

            if (m_rms_calibration_scale != 1.0){
                double diffdb = 20.0 * log10(m_rms_calibration_scale);
                ImPlot::SetupAxis(ImAxis_Y2, "dBu", ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines);
                ImPlot::SetupAxisLimits(ImAxis_Y2, -120 + diffdb, 20 + diffdb, ImPlotCond_Always);
            }

            if (m_compute_thd && m_fftfreqs)
            {
                char thdtext[64];
                snprintf(thdtext, 32, "THD : %.3f %%", m_thd);
                ImVec2 plotpos = ImPlot::GetPlotPos();
                ImVec2 plotsize = ImPlot::GetPlotSize();
                ImPlotPoint pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.1)));
                ImPlot::PlotText(thdtext, pnt.x, pnt.y);
                pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.14)));
                snprintf(thdtext, 32, "THD+N : %.3f %% (%.2f dB)", m_thdn, m_thddb);
                ImPlot::PlotText(thdtext, pnt.x, pnt.y);

                for (int i = m_fundamental_index; i < m_fft_found_peaks; ++i){
                    double fund[4] = {m_fft_highest_pos[i], m_fft_highest_pos[i], 40.0, -200.0};
                    ImPlot::PlotLine("Peaks", fund, fund+2, 2);
                    double y_pos = m_fftdraw[m_fft_highest_idx[i]];
                    snprintf(thdtext, 16, "%.4fdB", y_pos);
                    ImPlot::PlotText(thdtext, m_fft_highest_pos[i], y_pos);
                    double freq = m_fftfreqs[m_fft_highest_idx[i]] / 1000.0;
                    snprintf(thdtext, 16, "%.4fKHz", freq);
                    ImPlot::PlotText(thdtext, m_fft_highest_pos[i], y_pos - 8);
                }

                // THD+N clipping info
                // double range_min[4] = {m_fftfreqs[m_fft_fund_idx_range_min], m_fftfreqs[m_fft_fund_idx_range_min], 4200, -200};
                // ImPlot::PlotLine("Cut min", range_min, range_min+2, 2);
                // double range_max[4] = {m_fftfreqs[m_fft_fund_idx_range_max], m_fftfreqs[m_fft_fund_idx_range_max], 4200, -200};
                // ImPlot::PlotLine("Cut max", range_max, range_max+2, 2);
                ImPlot::PlotShaded("Fundamental detection", (double*)&m_fftfreqs[m_fft_fund_idx_range_min+1], (double*)&m_fftdraw[m_fft_fund_idx_range_min+1], m_fft_fund_idx_range_max - m_fft_fund_idx_range_min, -200.0);
            }


            double nf[4] = {0., (current_sample_rate)/2.0, m_noise_foor, m_noise_foor};
            ImPlot::PlotLine("Noise floor", nf, nf+2, 2);
            
            if (channelcount>0 && m_fftfreqs)
            {
                const char* buffer = NULL;
                if (m_fft_channel == 0){
                    buffer = "Audio left FFT";
                } else {
                    buffer = "Audio right FFT";
                }
                ImPlot::PlotLine(buffer, m_fftfreqs, m_fftdraw, m_sound_data_x.size()/2);
            }

            ImPlot::EndPlot();
        }
        ImGui::EndChild();

        ImGui::EndChild();

        if (must_reinit_recorder){
            if (m_measure_delay < m_recorder_latency_ms * 2){
                m_measure_delay = m_recorder_latency_ms * 2;
                m_sweep_timer.set(m_measure_delay);
            }
            reinit_recorder();
        }
    }

    bool compute(bool compute_fft = true, bool compute_noise_floor = true)
    {
        const int channelcount = m_audiorecorder.get_channel_count();
        if (channelcount == 0){
            return false;
        }

        const int fft_capture_size = m_capture_size / 2;
        const double current_sample_rate = m_audiorecorder.get_current_samplerate();
        const double half_sample_rate = current_sample_rate / 2.0;
        const double inv_current_sample_rate = 1.0 / current_sample_rate;
        const double inv_fft_capture_size = 1.0 / float(fft_capture_size);
        const double fft_step = half_sample_rate * inv_fft_capture_size;
        m_fft_highest_val = -100;
        
        m_sound_data1.resize(m_capture_size);
        m_sound_data2.resize(m_capture_size);
        m_audiorecorder.get_data(m_raw_buffer, m_capture_size * channelcount);
        m_sound_data_x.resize(m_capture_size);

        m_rms_left = m_rms_right = 0.0;

        // Fill audio waveform
        for (int i = 0; i < m_capture_size; i++){
            m_sound_data1[i] = m_raw_buffer[i*channelcount] * m_audio_gain;
                
            // THD test for non linear signal by applying small odd harmomics distortion
            // m_sound_data1[i] += 0.7f*sin(1000.*float(i) *2.f*3.14159*1./current_sample_rate);
            // if (m_sound_data1[i] > 0.f) m_sound_data1[i] = powf(m_sound_data1[i], 1.4);
            // if (m_sound_data1[i] < 0.f) m_sound_data1[i] = -powf(-m_sound_data1[i], 1.4f);
            if (m_fft_channel == 0){
                m_fftin[i] = m_sound_data1[i] * m_current_window_cache[i];
            } else {
                m_fftin[i] = m_sound_data2[i] * m_current_window_cache[i];
            }
            m_sound_data_x[i] = float(i) * inv_current_sample_rate * 1000.0;

            m_rms_left += m_sound_data1[i] * m_sound_data1[i];
            if(channelcount>1){
                m_sound_data2[i] = m_raw_buffer[i*channelcount+1] * m_audio_gain;
                m_rms_right += m_sound_data2[i] * m_sound_data2[i];
            }
            //m_sound_data2[i] += 0.7f*sin(1000.*float(i + 500) *2.f*3.14159*1./current_sample_rate);
        }

        m_rms_left = m_rms_left / m_capture_size;
        m_rms_left = sqrt(m_rms_left);

        if (channelcount > 1){
            m_rms_right = m_rms_right / m_capture_size;
            m_rms_right = sqrt(m_rms_right);
        }
        
        if (compute_fft){
            // Compute and fill audio FFT
            fftw_execute(m_fftplan);
            std::vector<double> fftdata(fft_capture_size);
            float sum = 0;
            for (int i = 0; i < fft_capture_size; ++i){
                m_fftfreqs[i] = fft_step * (double)(i);
                double fftout = sqrt(m_fftout[i][0] * m_fftout[i][0] + m_fftout[i][1] * m_fftout[i][1]) * inv_fft_capture_size;
                fftout *= m_window_amplitude_correction[m_fft_window_fn_index];
                fftout = std::max(20.0 * log10(fftout), -200.0);
                m_fftdraw[i] = isnan(fftout) ? -200.f : fftout;
                sum += fftout;
            }

            if (m_smooth_fft){
                sg_smooth(m_fftdraw, fftdata.data(), fft_capture_size, 5, 2);
                memcpy(m_fftdraw, fftdata.data(), fftdata.size()*4);
            }

            if (compute_noise_floor){
                double mean = sum * inv_fft_capture_size;
                double stddev = 0;
                for (int i = 0; i < fft_capture_size; ++i){
                    double a = (m_fftdraw[i] - mean);
                    stddev += a * a;
                }
                stddev = sqrt(stddev / float(fft_capture_size - 1));
                m_noise_foor = mean + stddev;
            } // compute_noise_floor
        } // compute_fft

        return true;
    }

    void compute_thdn()
    {
        const double invsqrt2 = 1.0 / sqrt(2.0);
        const double inv_capture_size = 1.0 / (double(m_capture_size/2));

        m_thdn = m_thddb = 0.;

        double max_val = -200;
        int max_val_index = 0;
        // Find fundamental
        for (int i = 0; i < m_capture_size/2; ++i){
            double fft_rms_sample = sqrt(m_fftout[i][0] * m_fftout[i][0] + m_fftout[i][1] * m_fftout[i][1]) * inv_capture_size * invsqrt2;
            m_rms_fft[i] = fft_rms_sample;
            if (fft_rms_sample > max_val){
                max_val = fft_rms_sample;
                max_val_index = i;
            }
        }

        double rms_fundamental = m_rms_fft[max_val_index];

        // Find FFT fundamental range
        double tmp = max_val;
        for (int i = max_val_index; i < m_capture_size/2; ++i){
            if (m_rms_fft[i] > tmp){
                m_fft_fund_idx_range_max = i;
                break;
            }
            tmp = m_rms_fft[i];
        }
        
        tmp = max_val;
        for (int i= max_val_index; i >= 0; --i){
            if (m_rms_fft[i] > tmp){
                m_fft_fund_idx_range_min = i;
                break;
            }
            tmp = m_rms_fft[i];
        }

        if (m_fft_fund_idx_range_max - m_fft_fund_idx_range_min <=0){
            m_fft_fund_idx_range_max = m_fft_fund_idx_range_min = 0;
            return;
        }

        double tot_rms = 0;
        double noise = 0;

        // Start at 1, we don't want DC value
        for (int i = 1; i < m_fft_fund_idx_range_min; ++i){
            noise += (m_rms_fft[i] * m_rms_fft[i]);
        }

        for (int i = m_fft_fund_idx_range_max; i < m_capture_size / 2; ++i){
            noise += (m_rms_fft[i] * m_rms_fft[i]);
        }
        
        m_thdn = sqrt(noise) / rms_fundamental;
        m_thddb = 20.0 * log10(m_thdn);
        m_thdn *= 100.0;
    }

    void compute_thd()
    {
        // Find peaks
        // Source : https://stackoverflow.com/questions/22583391/peak-signal-detection-in-realtime-timeseries-data
        const int fft_capture_size = m_capture_size / 2;

        smoothed_z_score(m_fftdraw, m_fftfiltered, fft_capture_size, m_zscore_lag, m_zscore_threshold, m_zscore_influence);
        int one_count = 0;
        int found = 0;

        for(int i = m_zscore_lag; i < fft_capture_size; ++i){
            double current_sample = m_fftfiltered[i];
            if (one_count == 0 && current_sample > 0){
                one_count++;
                continue;
            }
            if (one_count && current_sample > 0){
                one_count++;
                continue;
            }
            // We've found a valid range
            // Now let's find the max value inside
            if(one_count && current_sample < 1.0){
                int freq_start = i - one_count;
                double max = -130;
                int freq_idx = freq_start;
                // Find max value
                for (int j = freq_start; j < i; ++j){
                    if (m_fftdraw[j] > max){
                        freq_idx = j;
                        max = m_fftdraw[j];
                    }
                }
                m_fft_highest_idx[found] = freq_idx;
                m_fft_highest_pos[found++] = m_fftfreqs[freq_idx];
                if (found >= 200) break;
                one_count = 0;
            }
        }
        m_fft_found_peaks = found;

        // Find max values of filtered signal
        int fundamental_index = 0;
        double max = -200.;
        for(int i = 0; i < m_fft_found_peaks; ++i){
            if (m_fftdraw[m_fft_highest_idx[i]] > max){
                max = m_fftdraw[m_fft_highest_idx[i]];
                fundamental_index = i; 
            }
        }

        // Compute Total Harmonic Distortion
        // Source http://www.r-type.org/addtext/add183.htm
        if (m_fft_found_peaks){
            m_thd = 0;
            double fundamental_db = m_fftdraw[m_fft_highest_idx[fundamental_index]];
            double totdbc = 0;
            for (int i = fundamental_index + 1; i < m_fft_found_peaks; ++i){
                double dBc = m_fftdraw[m_fft_highest_idx[i]] - fundamental_db;
                totdbc += pow(10.0, dBc / 10.0);
            }

            m_thd = sqrt(totdbc) * 100.;
        }

        m_fundamental_index = fundamental_index;
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
                    reset_sine_generator();
                }
                ImGui::SameLine();
                const std::vector<std::string> out_samplerate = m_audio_out_idx >= 0 ? m_audiomanager.get_output_sample_rates_str(m_audio_out_idx) : std::vector<std::string>();
                if (ImGui::Combo("Samplerate##1", &m_out_sample_rate, vector_getter, (void*)&out_samplerate, out_samplerate.size()))
                {
                    reset_sine_generator();
                }
                
                ImGui::SeparatorText("Input device");
                const std::vector<std::string>& in_devices = m_audiomanager.get_input_devices();
                if (ImGui::Combo("Input", &m_combo_in, vector_getter, (void*)&in_devices, in_devices.size()))
                {
                    m_audio_in_idx = m_audiomanager.get_input_device_map(m_combo_in);
                    reinit_recorder();
                }
                ImGui::SameLine();
                const std::vector<std::string> in_samplerate = m_audio_in_idx >= 0 ? m_audiomanager.get_input_sample_rates_str(m_audio_in_idx) : std::vector<std::string>();
                if (ImGui::Combo("Samplerate##2", &m_in_sample_rate, vector_getter, (void*)&in_samplerate, in_samplerate.size()))
                {
                    reinit_recorder();
                }
                ImGui::PopItemWidth();
            }
            ImGui::End();
        }
    }

    void get_configuration_int(std::map<std::string, int> &cnf) override
    {
        cnf["logScaleFFT"] = m_logscale_frequency == true ? 1 : 0;
        cnf["smoothFFT"] = m_smooth_fft == true ? 1 : 0;
        cnf["FFTwindowType"] = m_fft_window_fn_index;
        cnf["showVoltmeter"] = m_show_rms_voltage == true ? 1 : 0;
        cnf["theme"] = m_uitheme;
    }

    void set_configuration_int(std::string s, int i) override
    {
        if (s == "logScaleFFT")
            m_logscale_frequency = i;
        else if (s == "smoothFFT")
            m_smooth_fft = i;
        else if (s == "FFTwindowType"){
            m_fft_window_fn_index = i;
            set_window_fn();
        }
        else if (s == "showVoltmeter")
            m_show_rms_voltage = i;
        else if (s == "theme"){
            m_uitheme = i;
            set_theme();
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

    window->set_minimum_window_size(1400, 800);

    app->add_window(window);
    app->run();
    return 0;
}