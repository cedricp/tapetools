#include "SDL.h"
#include "window_sdl.h"
#include "audio_sine_gen.h"
#include "audio_record.h"
#include "utils.h"
#include "timer.h"
#include <fftw3.h>

#define W 8
#define H 4
#define v ImVec2
#define dr(n,i)d->AddConvexPolyFilled(pp,6,(kd[n]>>(6-i))&1 ? IM_COL32(255,0,0,255) : ImGui::ColorConvertFloat4ToU32(bg))
char kd[]={0x7E,0x30,0x6D,0x79,0x33,0x5B,0x5F,0x70,0x7F,0x7B};
void digit(ImDrawList*d,int n,v e,v p){
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;
    ImVec4 bg = colors[ImGuiCol_WindowBg];
    bg.x += 0.05;bg.y += 0.05;bg.z += 0.05;
    float r[7][4]={{-1,-1,H,H},{1,-1,-H,H},{1,0,-H,-H},{-1,1,H,-W*1.5},{-1,0,H,-H},{-1,-1,H,H},{-1,0,H,-H},};
    for(int i=0;i<7;i++){
        v a,b;
        if(i%3==0){
            a=v(p.x+r[i][0]*e.x+r[i][2],p.y+r[i][1]*e.y+r[i][3]-H);
            b=v(a.x+e.x*2-W,a.y+W);
        }else{
            a=v(p.x+r[i][0]*e.x+r[i][2]-H,p.y+r[i][1]*e.y+r[i][3]);
            b=v(a.x+W,a.y+e.y-W);
        }
        v q = v(b.x-a.x, b.y-a.y);
        float s=W*0.6,u=s-H;
        if(q.x>q.y){
            v pp[]={{a.x+u,a.y+q.y*.5f},{a.x+s,a.y},{b.x-s,a.y},{b.x-u,a.y+q.y*.5f},{b.x-s,b.y},{a.x+s,b.y}};
            dr(n,i);
        }else{
            v pp[]={{a.x+q.x*.5f,a.y+u},{b.x,a.y+s},{b.x,b.y-s},{b.x-q.x*.5f,b.y-u},{a.x,b.y-s},{a.x,a.y+s}};
            dr(n,i);
        }
    }
}
#undef W
#undef H
#undef v
#undef dr

class AudioToolWindow : public Event, Widget
{
    audioManager m_audiomanager;
    audioSineGenerator m_sine_generator;
    audioRecorder m_audiorecorder;

    int  m_uitheme = 0;
    
    bool m_sine_generator_switch = false;
    int  m_pitch = 440;
    float m_sinegen_latency = 0.01f;
    int m_recorder_latency = 100;
    int m_sine_volume_db = 0.f;
    
    int m_audio_out_idx = -1;
    int m_audio_in_idx = -1;
    
    std::vector<float> m_sound_data1, m_sound_data2;
    std::vector<float> m_sound_data_x;
    std::vector<float> m_raw_buffer;
    fftwf_plan m_fftplan = NULL;
    float *m_fftin = nullptr;
    fftwf_complex *m_fftout = nullptr;
    float *m_fftdraw = nullptr;
    float *m_fftfreqs = nullptr;
    float *m_fftfiltered = nullptr;
    int m_capture_size = 0;
    float m_audio_gain = 1.0f;
    int m_combo_in = 0;
    int m_combo_out = 0;
    int m_in_sample_rate = 0;
    int m_out_sample_rate = 0;

    bool m_sound_setup_open = false;
    bool m_compute_thd = false;
    bool m_logscale_frequency = true;
    bool m_show_xy = false;
    bool m_show0db = false;
    float m_rms_calibration_scale = 1.0f;
    float m_scopezoom = 1;;
    std::vector<std::string> m_wmodes = {"Rectangle", "Hamming", "Hann-Poisson", "Blackman", "Blackman-Harris", "Hann", "Kaiser 5", "Kaiser 7"};
    std::vector<std::string> m_fftchannels = {"Left", "Right"};

    float (*m_window_fn)(int, int) = hann_fft_window;
    int     m_fft_window_fn = 5;
    int     m_fft_channel = 0;
    float   m_noise_foor = -100;
    float   m_fft_highest_pos[200];
    int     m_fft_highest_idx[200];
    float   m_fft_highest_val;
    int     m_fft_found_peaks = 0;
    bool    m_smooth_fft = true;
    float   m_thd = 0; 

    float   m_rms_left, m_rms_right;
    bool    m_show_rms_voltage = false;

    bool    m_sweep_started = false;
    int     m_sweep_current_frequency;
    int     m_sweep_span = 250;
    int     m_measure_delay = 400;
    std::vector<float> m_sweep_values;
    std::vector<float> m_sweep_freqs;
    Timer   m_sweep_timer;
    bool    m_pause_compute = false;

    bool    m_use_targetdb = false;
    bool    m_lockdb = false;
    float   m_target_db = 1.0f;
    float   m_locked_db_value = 0.f;

    STATIC_CALLBACK_METHOD(on_timer_event, AudioToolWindow)

public:
    AudioToolWindow(Window_SDL* win) : Widget(win, "AudioTools"), m_audiorecorder(m_audiomanager), m_sweep_timer(m_measure_delay, true)
    {
        set_maximized(true);
        set_movable(false);
        set_resizable(false);
        set_titlebar(false);

        m_audiomanager.flush();

        m_audio_out_idx = m_audiomanager.get_default_output_device_id();
        m_audio_in_idx = m_audiomanager.get_default_input_device_id();

        m_combo_in = m_audiomanager.get_input_device_reverse_map(m_audio_in_idx);
        m_combo_out = m_audiomanager.get_output_device_reverse_map(m_audio_out_idx);

        m_sine_generator.init(m_audiomanager, m_audio_out_idx, -1, m_sinegen_latency);

        reinit_recorder();

        CONNECT_CALLBACK((&m_sweep_timer), on_timer_event);
    }

    virtual ~AudioToolWindow(){
        m_sine_generator.destroy();
        destroy_capture();
    }

    void destroy_capture()
    {
        if (m_fftplan) fftwf_destroy_plan(m_fftplan);

        delete[] m_fftin;
        delete[] m_fftout;
        delete[] m_fftdraw;
        delete[] m_fftfreqs;
        delete[] m_fftfiltered;
        m_sound_data_x.clear();

        m_fftin = nullptr;
        m_fftout = nullptr;
        m_fftdraw = nullptr;
        m_fftfreqs = nullptr;
        m_fftfiltered = nullptr;
        m_fftplan = nullptr;
    }

    void init_capture()
    {
        int capture_size = m_audiorecorder.get_buffer_size(float(m_recorder_latency) / 1000.f, false);
        if (capture_size == 0){
            return;
        }
        destroy_capture(); 
        m_capture_size = capture_size;
        m_fftin = new float[capture_size];
        m_fftout = new fftwf_complex[capture_size];
        m_fftdraw = new float[capture_size/2];
        m_fftfreqs = new float[capture_size/2];   
        m_fftfiltered = new float[capture_size/2];   
        m_fftplan = fftwf_plan_dft_r2c_1d(capture_size, m_fftin, m_fftout, FFTW_MEASURE | FFTW_PRESERVE_INPUT );
        m_fft_channel = 0;
    }

    void reinit_recorder()
    {
        if (m_audio_in_idx < 0){
            return;
        }

            if (m_audiorecorder.init(float(m_recorder_latency) / 1000.f, m_audio_in_idx, m_audiomanager.get_input_sample_rates(m_audio_in_idx)[m_in_sample_rate]))
            {
                m_audiorecorder.start();
            }
        init_capture();
    }

    void reset_sine_generator(){
        int current_sine_samplerate = m_audiomanager.get_output_sample_rates(m_audio_out_idx)[m_out_sample_rate];
        m_sine_generator.destroy();
        m_sine_generator.init(m_audiomanager, m_audio_out_idx, current_sine_samplerate, m_sinegen_latency);
        m_sine_generator.set_pitch(m_pitch);
        m_sine_generator.start();
        m_sine_generator.pause(!m_sine_generator_switch);
    }

    CALLBACK_METHOD(on_timer_event){
        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        float fft_step = m_capture_size / current_sample_rate;
        
        if (m_sweep_current_frequency >= 20000){
            // We reached the end of the measure
            stop_sweep_gen();
            return;
        }

        compute();

        int min_freq_idx = std::max(int((m_sweep_current_frequency-500)*fft_step), 0);
        int max_freq_idx = std::min(int((m_sweep_current_frequency+500)*fft_step), m_capture_size / 2);

        float max_val = m_noise_foor;
        for (int i = min_freq_idx; i < max_freq_idx; ++i){
            if (m_fftdraw[i] > max_val){
                max_val = m_fftdraw[i];
            }
        }

        m_sweep_values.push_back(max_val);
        m_sweep_freqs.push_back(m_sweep_current_frequency);


        m_sweep_current_frequency += m_sweep_span;
        m_sine_generator.set_pitch(m_sweep_current_frequency);
        m_sweep_timer.start();
    }

    void set_theme(){
        if (m_uitheme == 0){
            ImGui::StyleColorsDark();   
        }
        if (m_uitheme == 1){
            ImGui::StyleColorsLight();   
        }
        if (m_uitheme == 2){
            ImGui::StyleColorsClassic();   
        }
    }

    void draw() override {
        m_audiomanager.flush();
        int channelcount = m_audiorecorder.get_channel_count();
    
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Preferences")){
                ImGui::MenuItem("Sound card setup", nullptr, &m_sound_setup_open);
                
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

    void start_sweep_gen(){
        m_sweep_started = true;
        m_sweep_current_frequency = 20;
        m_sweep_freqs.clear();
        m_sweep_values.clear();
        m_sine_generator_switch = true;
        reset_sine_generator();
        m_sine_generator.set_pitch(m_sweep_current_frequency);
        m_sweep_timer.start();
        //m_pause_compute = true;
    }

    void stop_sweep_gen(){
        m_sweep_started = false;
        m_sine_generator_switch = false;
        m_sine_generator.pause();
        m_sweep_timer.stop();
        //m_pause_compute = false;
    }

    void set_window_fn(){
        switch (m_fft_window_fn){
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
    }
    void draw_sweep_tab(){
        if(!m_pause_compute) compute();

        int channelcount = m_audiorecorder.get_channel_count(); 
        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        float frameh = ImGui::GetFrameHeightWithSpacing();
        float padh = 3.0f * ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ItemSpacing.y;

        ImGui::BeginChild("ScopesChild1", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);

        ImGui::BeginChild("ScopesChild2", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (!m_sweep_started){
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
        if(ImGui::DragInt("Delay (ms)", &m_measure_delay, 1.f, m_recorder_latency * 2, 3000)){
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
        if (ImGui::Combo("Window mode", &m_fft_window_fn, vector_getter, (void *)&m_wmodes, m_wmodes.size()))
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
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 20.f, 20000.f);

            if (channelcount>0 && m_fftfreqs)
            {
                ImPlot::PlotLine("Audio FFT", m_fftfreqs, m_fftdraw, m_sound_data_x.size()/2);
                float nf[4] = {0., (current_sample_rate)/2.f, m_noise_foor, m_noise_foor};
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
        snprintf(voltmeter, 20, "%.4f", value);

        ImGui::InvisibleButton("canvas", size);
        ImVec2 p0 = ImGui::GetItemRectMin();
        ImVec2 p1 = ImGui::GetItemRectMax();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->PushClipRect(p0, p1);

        int textsize = strlen(voltmeter);
        float p=0.02*size.x,s=size.x/textsize-p,x=s*.5,y=size.y*.5;
        for(int i=textsize-1;i>=0;i--){
            if (voltmeter[i] >= '0' && voltmeter[i] <= '9'){
                int _d = voltmeter[i] - '0';
                digit(draw_list,_d,ImVec2(s*.5,y),ImVec2(p1.x-x,p0.y+y));
                x+=s+p;
            } else {
                draw_list->AddCircleFilled(ImVec2(p1.x-x,p0.y+(2.f*y) -12.f), 4.f, IM_COL32(255 ,0 ,0, 255), 8);
                x+=s/2+p;
            }
        }
        draw_list->PopClipRect();
    }

    void draw_rt_analysis_tab(){
        int channelcount = m_audiorecorder.get_channel_count(); 
        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        bool must_reinit_recorder = false;

        if (!m_pause_compute && compute() && m_compute_thd)
        {
            compute_thd();
        }

        float frameh = ImGui::GetFrameHeightWithSpacing();
        float padh = 3.0f * ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ItemSpacing.y;

        /*
        * Tone Generator
        */
        ImGui::BeginChild("ScopesChildToneGen", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);

        ImGui::BeginChild("ScopesChildTonGenSwitch", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if(ImGui::ToggleButton("Tone generator", &m_sine_generator_switch)){
            reset_sine_generator();
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildToneFreq", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (ImGui::SliderInt("Pitch", &m_pitch, 20, 20000)){
            m_sine_generator.set_pitch(m_pitch);
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildToneVol", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (ImGui::SliderInt("Intensity", &m_sine_volume_db, -100, 0, "%d dB")){
            m_sine_generator.set_volume(m_sine_volume_db);
        }
        ImGui::EndChild();

        ImGui::EndChild();

        /*
        * Time domain analysis
        */

        ImGui::BeginChild("ScopesChild", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
        float plotheight = height() / 2.0f - 5.f;


        ImGui::BeginChild("ScopesChild1", ImVec2(0, plotheight), ImGuiChildFlags_Border, ImGuiWindowFlags_None);

        if (channelcount > 1){
            ImGui::BeginChild("ScopesChildShowXY", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
            ImGui::ToggleButton("XY diagram", &m_show_xy);
            ImGui::EndChild();
            ImGui::SameLine();
        }

        ImGui::BeginChild("ScopesChildShowRmsVolts", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Show voltmeter", &m_show_rms_voltage);
        ImGui::EndChild();
        ImGui::SameLine();
       
        ImGui::BeginChild("ScopesChildYzoom", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(70);
        ImGui::SliderFloat("Amplitude mult", &m_scopezoom, 1, 50);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildCalib", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        static float rms_calibration = 1.f;
        if (m_rms_calibration_scale == 1.f){
            ImGui::SetNextItemWidth(70);
            ImGui::InputFloat("Measured RMS", &rms_calibration);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            if (ImGui::Button("Calibrate from left")){

                m_rms_calibration_scale = rms_calibration / m_rms_left;
            }
            if (channelcount > 1){
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                if (ImGui::Button("Calibrate from right")){
                    m_rms_calibration_scale = rms_calibration / m_rms_right;
                }
            }
        }
        if (m_rms_calibration_scale != 1.f){
            ImGui::SameLine();
            if (ImGui::Button("Clear calibration")){

                m_rms_calibration_scale = 1.0f;
                rms_calibration = 1.f;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildShow0db", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Show 0dBm Ref", &m_show0db);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildTargetVolt", ImVec2(-1, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Show dB target", &m_use_targetdb);
        if (m_use_targetdb){
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            ImGui::SliderFloat("dB target", &m_target_db, -20, 20);
            ImGui::SameLine();
            ImGui::ToggleButton("Lock", &m_lockdb);
            if (m_lockdb){
                float target_val_left = 1.f - fabsf( m_locked_db_value - m_rms_left * m_rms_calibration_scale ) * 10.f;
                float target_val_right = 1.f - fabsf( m_locked_db_value - m_rms_right * m_rms_calibration_scale ) *10.f;
                ImGui::SameLine();
                ImGui::ProgressBar(target_val_left, ImVec2(70,0));
                ImGui::SameLine();
                ImGui::ProgressBar(target_val_right, ImVec2(70,0));
            }
        }
        ImGui::EndChild();

        if (m_show_rms_voltage){
            ImGui::BeginChild("ScopesChildVoltageLcd", ImVec2(-1, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiWindowFlags_None);
            
            ImGui::BeginChild("ScopesChildVoltageLcd1", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
            draw_lcd(m_rms_left * m_rms_calibration_scale, ImVec2(200, 70));
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Volts RMS Left");
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::SetCursorPosX(width());
            ImGui::BeginChild("ScopesChildVoltageLcd2", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
            draw_lcd(m_rms_right * m_rms_calibration_scale, ImVec2(200, 70));
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Volts RMS Right");
            ImGui::EndChild();

            ImGui::EndChild();
        }

        if (ImPlot::BeginPlot("Audio", ImVec2(m_show_xy ? width()-plotheight-10 : -1, -1)))
        {
            float x_limit = 1.0f / m_scopezoom;
            float xmax = current_sample_rate > 0 ? float(m_capture_size) * (1.f / (current_sample_rate * 0.001f)) : INFINITY;
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, xmax);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -x_limit, x_limit, ImPlotCond_Always);

            if (m_rms_calibration_scale != 1.f){
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
                float rms[4] = {0., (current_sample_rate)/2.f, m_rms_left, m_rms_left};
                ImPlot::PlotLine("signal RMS left", rms, rms+2, 2);
            }
            if (channelcount > 1){
                float rms[4] = {0., (current_sample_rate)/2.f, m_rms_right, m_rms_right};
                ImPlot::PlotLine("signal RMS right", rms, rms+2, 2);
            }

            if(m_use_targetdb){
                if (!m_lockdb){
                    m_locked_db_value = m_rms_left * powf(10, m_target_db/20.f);
                }
                float tgtpnt[4] = {0., (current_sample_rate)/2.f, m_locked_db_value, m_locked_db_value};
                ImPlot::PlotLine("target dB", tgtpnt, tgtpnt+2, 2);
            }

            if (m_show0db){
                float zerodb = .775f / m_rms_calibration_scale;
                float rms[4] = {0., (current_sample_rate)/2.f, zerodb, zerodb};
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
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildSmoothFFT", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Smooth FFT", &m_smooth_fft);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild5", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("Compute THD", &m_compute_thd);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild6", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Window mode", &m_fft_window_fn, vector_getter, (void *)&m_wmodes, m_wmodes.size()))
        {
            set_window_fn();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild7", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("Channel", &m_fft_channel, vector_getter, (void *)&m_fftchannels, m_fftchannels.size());
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild8", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(100);
        if(ImGui::InputInt("Capture size (ms)", &m_recorder_latency, 100, 200, ImGuiInputTextFlags_EnterReturnsTrue)){
            if(m_recorder_latency < 100) m_recorder_latency = 100;
            if(m_recorder_latency > 1000) m_recorder_latency = 1000;
            must_reinit_recorder = true;
        }
        ImGui::EndChild();

        ImGui::EndChild();

        if (ImPlot::BeginPlot("AudioFFT", ImVec2(-1, -1))){
            float xfftmax = current_sample_rate > 0 ? (current_sample_rate)/2.f : INFINITY;
            ImPlot::SetupAxes("Frequency", "dB FullScale", 0, ImPlotAxisFlags_Lock);
            if (m_logscale_frequency){
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
            }
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
            ImPlot::SetupAxesLimits(20.f, xfftmax, -130.0, 0.0);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 20.f, 20000.f);

            if (m_compute_thd)
            {
                char thdtext[16];
                snprintf(thdtext, 16, "THD : %.6f %%", m_thd);
                ImVec2 plotpos = ImPlot::GetPlotPos();
                ImVec2 plotsize = ImPlot::GetPlotSize();
                ImPlotPoint pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.3)));
                ImPlot::PlotText(thdtext, pnt.x, pnt.y);

                for (int i = 0; i < m_fft_found_peaks; ++i){
                    float fund[4] = {m_fft_highest_pos[i], m_fft_highest_pos[i], 0.f, -200.f};
                    ImPlot::PlotLine("Peaks", fund, fund+2, 2);
                    float y_pos = m_fftdraw[m_fft_highest_idx[i]];
                    snprintf(thdtext, 16, "%.4fdB", y_pos);
                    ImPlot::PlotText(thdtext, m_fft_highest_pos[i], y_pos);
                    float freq = m_fftfreqs[m_fft_highest_idx[i]] / 1000.f;
                    snprintf(thdtext, 16, "%.4fKHz", freq);
                    ImPlot::PlotText(thdtext, m_fft_highest_pos[i], y_pos - 8);
                }
            }
            float nf[4] = {0., (current_sample_rate)/2.f, m_noise_foor, m_noise_foor};
            ImPlot::PlotLine("Noise floor", nf, nf+2, 2);
            
            if (channelcount>0 && m_fftfreqs)
            {
                ImPlot::PlotLine("Audio left FFT", m_fftfreqs, m_fftdraw, m_sound_data_x.size()/2);

            }

            ImPlot::EndPlot();
        }
        ImGui::EndChild();

        ImGui::EndChild();

        if (must_reinit_recorder){
            if (m_measure_delay < m_recorder_latency * 2){
                m_measure_delay = m_recorder_latency * 2;
                m_sweep_timer.set(m_measure_delay);
            }
            reinit_recorder();
        }
    }

    bool compute(bool compute_fft = true, bool compute_noise_floor = true){
        const int channelcount = m_audiorecorder.get_channel_count();

        if (channelcount == 0 || m_audiorecorder.get_available_samples() < m_capture_size * channelcount){
            return false;
        }

        const float current_sample_rate = m_audiorecorder.get_current_samplerate();
        const float half_sample_rate = current_sample_rate / 2.f;
        const float inv_current_sample_rate = 1.0f / current_sample_rate;
        const int fft_capture_size = m_capture_size / 2;
        const float inv_fft_capture_size = 1.0f / float(fft_capture_size);
        const float fft_step = half_sample_rate * inv_fft_capture_size;
        m_fft_highest_val = -100;
        
        m_sound_data1.resize(m_capture_size);
        m_sound_data2.resize(m_capture_size);
        m_audiorecorder.get_data(m_raw_buffer, m_capture_size * channelcount);
        m_sound_data_x.resize(m_capture_size);

        m_rms_left = m_rms_right = 0.f;

        // Fill audio waveform
        for (int i = 0; i < m_capture_size; i++){
            m_sound_data1[i] = m_raw_buffer[i*channelcount] * m_audio_gain;
                
            // THD test for non linear signal by applying small odd harmomics distortion
            // m_sound_data1[i] += 0.7f*sin(1000.*float(i) *2.f*3.14159*1./current_sample_rate);
            // if (m_sound_data1[i] > 0.f) m_sound_data1[i] = powf(m_sound_data1[i], 1.4);
            // if (m_sound_data1[i] < 0.f) m_sound_data1[i] = -powf(-m_sound_data1[i], 1.4f);
            if (m_fft_channel == 0){
                m_fftin[i] = m_sound_data1[i] * m_window_fn(i, m_capture_size);
            } else {
                m_fftin[i] = m_sound_data2[i] * m_window_fn(i, m_capture_size);
            }
            m_sound_data_x[i] = float(i) * inv_current_sample_rate * 1000.f;

            m_rms_left += m_sound_data1[i] * m_sound_data1[i];
            if(channelcount>1){
                m_sound_data2[i] = m_raw_buffer[i*channelcount+1] * m_audio_gain;
                m_rms_right += m_sound_data2[i] * m_sound_data2[i];
            }
            //m_sound_data2[i] += 0.7f*sin(1000.*float(i + 500) *2.f*3.14159*1./current_sample_rate);
        }

        m_rms_left = m_rms_left / m_capture_size;
        m_rms_left = sqrtf(m_rms_left);

        if (channelcount > 1){
            m_rms_right = m_rms_right / m_capture_size;
            m_rms_right = sqrtf(m_rms_right);
        }
        
        if (compute_fft){
            // Compute and fill audio FFT
            fftwf_execute(m_fftplan);
            std::vector<float> fftdata(fft_capture_size);
            float sum = 0;
            for (int i = 0; i < fft_capture_size; ++i){
                m_fftfreqs[i] = fft_step * (float)(i);
                float fftout = sqrtf(m_fftout[i][0] * m_fftout[i][0] + m_fftout[i][1] * m_fftout[i][1]) * inv_fft_capture_size;
                fftout = std::max(20.f * log10(fftout), -200.f);
                m_fftdraw[i] = isnan(fftout) ? -200.f : fftout;
                sum += fftout;
            }

            if (m_smooth_fft){
                sg_smooth(m_fftdraw, fftdata.data(), fft_capture_size, 5, 0);
                memcpy(m_fftdraw, fftdata.data(), fftdata.size()*4);
            }

            if (compute_noise_floor){
                float mean = sum / fft_capture_size;
                float stddev = 0;
                for (int i = 0; i < fft_capture_size; ++i){
                    float a = (m_fftdraw[i] - mean);
                    stddev += a * a;
                }
                stddev = sqrtf(stddev / float(fft_capture_size - 1));
                m_noise_foor = mean + stddev;
            } // compute_noise_floor
        } // compute_fft

        return true;
    }

    void compute_thd(){
        // Find peaks
        // Source : https://stackoverflow.com/questions/22583391/peak-signal-detection-in-realtime-timeseries-data
        const int fft_capture_size = m_capture_size / 2;

        smoothed_z_score(m_fftdraw, m_fftfiltered, fft_capture_size, 50, 4, 0.f);
        int one_count = 0;
        int found = 0;

        for(int i = 50; i < fft_capture_size; ++i){
            float current_sample = m_fftfiltered[i];
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
            if(one_count && current_sample < 1.f){
                int freq_start = i - one_count;
                float max = -130;
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
        int fundamental_idx = 0;
        float max = -200.;
        for(int i = 0; i < m_fft_found_peaks; ++i){
            if (m_fftdraw[m_fft_highest_idx[i]] > max){
                max = m_fftdraw[m_fft_highest_idx[i]];
                fundamental_idx = i; 
            }
        }

        // Compute Total Harmonic Distortion
        // Source http://www.r-type.org/addtext/add183.htm
        if (m_fft_found_peaks){
            m_thd = 0;
            float fundamental_db = m_fftdraw[m_fft_highest_idx[fundamental_idx]];

            for (int i = fundamental_idx + 1; i < m_fft_found_peaks; ++i){
                float dBc = m_fftdraw[m_fft_highest_idx[i]] - fundamental_db;
                float v_rms = powf(10.f, dBc/10.f);
                m_thd += v_rms ; 
            }

            m_thd = sqrtf(m_thd) * 100.f;
        }
    }

    void draw_tools_windows(){
        if (m_sound_setup_open && !m_sweep_started){
            ImGui::SetNextWindowSize(ImVec2(600, 150));
            if(ImGui::Begin("Sound card setup", &m_sound_setup_open)){
                ImVec2 winsize = ImGui::GetWindowSize();
                ImGui::PushItemWidth(winsize.x / 3);
                const std::vector<std::string>& out_devices = m_audiomanager.get_output_devices();
                ImGui::SeparatorText("Output device");
                if (ImGui::Combo("Ouput", &m_combo_out, vector_getter, (void*)&out_devices, out_devices.size())){
                    m_audio_out_idx = m_audiomanager.get_output_device_map(m_combo_out);
                    reset_sine_generator();
                }
                ImGui::SameLine();
                const std::vector<std::string> out_samplerate = m_audio_out_idx >= 0 ? m_audiomanager.get_output_sample_rates_str(m_audio_out_idx) : std::vector<std::string>();
                if (ImGui::Combo("Samplerate##1", &m_out_sample_rate, vector_getter, (void*)&out_samplerate, out_samplerate.size())){
                    reset_sine_generator();
                }
                
                ImGui::SeparatorText("Input device");
                const std::vector<std::string>& in_devices = m_audiomanager.get_input_devices();
                if (ImGui::Combo("Input", &m_combo_in, vector_getter, (void*)&in_devices, in_devices.size())){
                    m_audio_in_idx = m_audiomanager.get_input_device_map(m_combo_in);
                    reinit_recorder();
                }
                ImGui::SameLine();
                const std::vector<std::string> in_samplerate = m_audio_in_idx >= 0 ? m_audiomanager.get_input_sample_rates_str(m_audio_in_idx) : std::vector<std::string>();
                if (ImGui::Combo("Samplerate##2", &m_in_sample_rate, vector_getter, (void*)&in_samplerate, in_samplerate.size())){
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
        cnf["FFTwindowType"] = m_fft_window_fn;
        cnf["showVoltmeter"] = m_show_rms_voltage == true ? 1 : 0;
        cnf["theme"] = m_uitheme;
    }

    void get_configuration_float(std::map<std::string, float> &cnf) override
    {
        cnf["calibrationValue"] = m_rms_calibration_scale;
    }

    void set_configuration_int(std::string s, int i) override
    {
        if (s == "logScaleFFT")
            m_logscale_frequency = i;
        if (s == "smoothFFT")
            m_smooth_fft = i;
        if (s == "FFTwindowType")
            m_fft_window_fn = i;
        if (s == "showVoltmeter")
            m_show_rms_voltage = i;
        if (s == "theme"){
            m_uitheme = i;
            set_theme();
        }
    }

    void set_configuration_float(std::string s, float f) override
    {
        printf("%s %f\n", s.c_str(), f);
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
        m_audiotool = new AudioToolWindow(this);
    }

    virtual ~MainWindow(){
    }

    void draw(bool c) override {
        Window_SDL::draw(c);
    }
};

int main(int argc, char *argv[])
{
    App_SDL *app = App_SDL::get();
    Window_SDL *window = new MainWindow;

    ImGui::GetStyle().FrameRounding = 5.0;
    ImGui::GetStyle().ChildRounding = 5.0;
    ImGui::GetStyle().WindowRounding = 4.0;
    ImGui::GetStyle().GrabRounding = 4.0;
    ImGui::GetStyle().GrabMinSize = 4.0;
    window->set_minimum_window_size(1400, 800);
    app->add_window(window);
    app->run();
    return 0;
}