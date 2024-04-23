#include "SDL.h"
#include "window_sdl.h"
#include "audio_sine_gen.h"
#include "audio_record.h"
#include "utils.h"
#include "timer.h"
#include <fftw3.h>

class AudioToolWindow : public Event, Widget
{
    audioManager m_audiomanager;
    audioSineGenerator m_sine_generator;
    audioRecorder m_audiorecorder;
    
    bool m_sine_generator_switch = false;
    float m_pitch = 440;
    
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
    float m_latency = 0.2f;

    bool m_sound_setup_open = false;
    bool m_tone_generator_open = false;
    bool m_compute_thd = false;
    bool m_logscale_frequency = true;

    float (*m_window_fn)(int, int) = hann_fft_window;
    int     m_fft_window_fn = 5;
    int     m_fft_channel = 0;
    float   m_noise_foor = -100;
    float   m_fft_highest_pos[200];
    int     m_fft_highest_idx[200];
    float   m_fft_highest_val;
    int     m_fft_found_peaks = 0;
    float   m_thd = 0; 

    bool    m_sweep_started = false;
    int     m_sweep_current_frequency;
    int     m_sweep_span = 250.f;
    std::vector<float> m_sweep_values;
    std::vector<float> m_sweep_freqs;
    Timer   m_sweep_timer;

    STATIC_CALLBACK_METHOD(on_timer_event, AudioToolWindow)

public:
    AudioToolWindow(Window_SDL* win) : Widget(win, "AudioTools"), m_audiorecorder(m_audiomanager), m_sweep_timer(200, true)
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

        m_sine_generator.init(m_audiomanager, m_audio_out_idx, -1, 0.1f);

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

        m_fftin = nullptr;
        m_fftout = nullptr;
        m_fftdraw = nullptr;
        m_fftfreqs = nullptr;
        m_fftfiltered = nullptr;
        m_fftplan = nullptr;
    }

    void init_capture()
    {
        int capture_size = m_audiorecorder.get_buffer_size(m_latency, false);
        if (capture_size == 0){
            return;
        }
        destroy_capture(); 
        m_fftin = new float[capture_size];
        m_fftout = new fftwf_complex[capture_size];
        m_fftdraw = new float[capture_size/2];
        m_fftfreqs = new float[capture_size/2];   
        m_fftfiltered = new float[capture_size/2];   
        m_fftplan = fftwf_plan_dft_r2c_1d(capture_size, m_fftin, m_fftout, FFTW_MEASURE | FFTW_PRESERVE_INPUT );
        m_capture_size = capture_size;
        m_fft_channel = 0;
    }

    void reinit_recorder()
    {
        if (m_audiorecorder.init(m_latency, m_audio_in_idx, m_audiomanager.get_input_sample_rates(m_audio_in_idx)[m_in_sample_rate]))
        {
            m_audiorecorder.start();
        }
        init_capture();
    }

    void reset_sine_generator(){
        int current_sine_samplerate = m_audiomanager.get_output_sample_rates(m_audio_out_idx)[m_out_sample_rate];
        m_sine_generator.destroy();
        m_sine_generator.init(m_audiomanager, m_audio_out_idx, current_sine_samplerate, 0.1f);
        m_sine_generator.start();
        m_sine_generator.pause(!m_sine_generator_switch);
    }

    CALLBACK_METHOD(on_timer_event){
        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        float fft_step = m_capture_size / current_sample_rate;
        
        if (m_sweep_current_frequency >= (current_sample_rate*0.5f)){
            // We reached the end of the measure
            m_sweep_started = false;
            m_sine_generator.pause();
            return;
        }

        compute();

        int min_freq_idx = std::max((m_sweep_current_frequency-500)*fft_step, 0.f);
        int max_freq_idx = std::min((m_sweep_current_frequency+500)*fft_step, float(m_capture_size / 2));

        printf("%i %i \n", min_freq_idx, max_freq_idx);

        float max_val = m_noise_foor;
        for (int i = min_freq_idx; i < max_freq_idx; ++i){
            if (m_fftdraw[i] > max_val){
                max_val = m_fftdraw[i];
            }
        }

        m_sweep_values.push_back(max_val);
        m_sweep_freqs.push_back(m_sweep_current_frequency);


        m_sweep_current_frequency += m_sweep_span;
        m_sine_generator.setPitch(m_sweep_current_frequency);
        m_sweep_timer.start();
    }

    void draw() override {
        m_audiomanager.flush();
        int channelcount = m_audiorecorder.get_channel_count();
    
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Preferences")){
                ImGui::MenuItem("Sound card setup", nullptr, &m_sound_setup_open);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Tools")){
                ImGui::MenuItem("Tone generator", nullptr, &m_tone_generator_open);
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

    void draw_sweep_tab(){
        compute();

        int channelcount = m_audiorecorder.get_channel_count(); 
        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        float frameh = ImGui::GetFrameHeightWithSpacing();
        float padh = 3.0f * ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ItemSpacing.y;

        ImGui::BeginChild("ScopesChild", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
        float plotheight = height() / 2.0f - 5.f;

        ImGui::BeginChild("ScopesChild2", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("LOGFREG", &m_logscale_frequency);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();ImGui::Text("Log scale frequency");
        ImGui::EndChild();
        ImGui::SameLine();
        
        ImGui::BeginChild("ScopesChild3", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY , ImGuiWindowFlags_None);
        if (!m_sweep_started){
            if (ImGui::Button("Start")){
                m_sweep_started = true;
                m_sweep_current_frequency = 20;
                m_sweep_freqs.clear();
                m_sweep_values.clear();
                m_sine_generator_switch = true;
                reset_sine_generator();
                m_sine_generator.setPitch(m_sweep_current_frequency);
                m_sweep_timer.start();
            }
        } else {
            if (ImGui::Button("Stop")){
                m_sweep_started = false;
                m_sine_generator_switch = false;
                m_sine_generator.pause();
                m_sweep_timer.stop();
            }
        }
        ImGui::EndChild();


        if (ImPlot::BeginPlot("AudioFFT", ImVec2(-1, -1))){
            float xfftmax = current_sample_rate > 0 ? (current_sample_rate)/2.f : INFINITY;
            ImPlot::SetupAxes("Frequency", "dB FullScale", 0, ImPlotAxisFlags_Lock);
            if (m_logscale_frequency){
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
            }
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
            ImPlot::SetupAxesLimits(20.f, xfftmax, -130.0, 0.0);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 20.f, xfftmax);

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

    void draw_rt_analysis_tab(){
        int channelcount = m_audiorecorder.get_channel_count(); 
        float current_sample_rate = m_audiorecorder.get_current_samplerate();

        if (compute() && m_compute_thd)
        {
            compute_thd();
        }

        float frameh = ImGui::GetFrameHeightWithSpacing();
        float padh = 3.0f * ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ItemSpacing.y;

        ImGui::BeginChild("ScopesChild", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
        float plotheight = height() / 2.0f - 5.f;

        ImGui::BeginChild("ScopesChild1", ImVec2(0, plotheight), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
        if (ImPlot::BeginPlot("Audio", ImVec2(-1, -1)))
        {
            float xmax = current_sample_rate > 0 ? float(m_capture_size) * (1.f / (current_sample_rate * 0.001f)) : INFINITY;
            ImPlot::SetupAxes("Time (ms)", "Amplitude", 0, ImPlotAxisFlags_Lock);
            ImPlot::SetupAxesLimits(0, xmax, -1.f, 1.f);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, xmax);
            if (channelcount > 0)
                ImPlot::PlotLine("Channel 1", m_sound_data_x.data(), m_sound_data1.data(), m_sound_data_x.size());
            if (channelcount > 1)
                ImPlot::PlotLine("Channel 2", m_sound_data_x.data(), m_sound_data2.data(), m_sound_data_x.size());
            ImPlot::EndPlot();
        }

        ImGui::EndChild();

        std::vector<std::string> wmodes = {"Rectangle", "Hamming", "Hann-Poisson", "Blackman", "Blackman-Harris", "Hann", "Keiser 5"};
        std::vector<std::string> fftchannels = {"Left", "Right"};

        ImGui::BeginChild("ScopesChild2", ImVec2(0, plotheight), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
        ImGui::BeginChild("ScopesChild3", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);
        ImGui::BeginChild("ScopesChild4", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("LogFreq", &m_logscale_frequency);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Log scale frequency");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild5", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("CTHD", &m_compute_thd);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Compute THD");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild6", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Window mode", &m_fft_window_fn, vector_getter, (void *)&wmodes, wmodes.size()))
        {
            if (m_fft_window_fn == 0) m_window_fn = rectangle_fft_window;
            else if (m_fft_window_fn == 1) m_window_fn = hamming_fft_window;
            else if (m_fft_window_fn == 2) m_window_fn = hann_poisson_fft_window;
            else if (m_fft_window_fn == 3) m_window_fn = blackman_fft_window;
            else if (m_fft_window_fn == 4) m_window_fn = blackman_harris_fft_window;
            else if (m_fft_window_fn == 5) m_window_fn = hann_fft_window;
            else if (m_fft_window_fn == 6) m_window_fn = keiser5_fft_window;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChild7", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("Channel", &m_fft_channel, vector_getter, (void *)&fftchannels, fftchannels.size());
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
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 20.f, xfftmax);
            // ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -130., 0);

            if (m_compute_thd)
            {
                char thdtext[16];
                snprintf(thdtext, 16, "THD : %.2f %%", m_thd);
                ImVec2 plotpos = ImPlot::GetPlotPos();
                ImVec2 plotsize = ImPlot::GetPlotSize();
                ImPlotPoint pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.3)));
                ImPlot::PlotText(thdtext, pnt.x, pnt.y);

                for (int i = 0; i < m_fft_found_peaks; ++i){
                    float fund[4] = {m_fft_highest_pos[i], m_fft_highest_pos[i], 0., -130.};
                    ImPlot::PlotLine("Peaks", fund, fund+2, 2);
                }
            }
            float nf[4] = {0., (current_sample_rate)/2.f, m_noise_foor, m_noise_foor};
            ImPlot::PlotLine("Noise floor", nf, nf+2, 2);
            
            if (channelcount>0 && m_fftfreqs)
            {
                ImPlot::PlotLine("Audio FFT", m_fftfreqs, m_fftdraw, m_sound_data_x.size()/2);
            }

            ImPlot::EndPlot();
        }
        ImGui::EndChild();

        ImGui::EndChild();
    }

    bool compute(bool compute_fft = true, bool compute_noise_floor = true){
        int channelcount = m_audiorecorder.get_channel_count();

        if (channelcount == 0 || m_audiorecorder.get_available_samples() < m_capture_size * channelcount){
            return false;
        }

        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        float inv_current_sample_rate = 1.0f / current_sample_rate;
        const int fft_capture_size = m_capture_size / 2;
        const float inv_fft_capture_size = 1.0f / float(fft_capture_size);
        m_fft_highest_val = -100;
        
        m_sound_data1.resize(m_capture_size);
        m_sound_data2.resize(m_capture_size);
        m_audiorecorder.get_data(m_raw_buffer, m_capture_size * channelcount);
        m_sound_data_x.resize(m_capture_size);

        // Fill audio waveform
        for (int i = 0; i < m_capture_size; i++){
            m_sound_data1[i] = m_raw_buffer[i*channelcount] * m_audio_gain;
            // THD test for non linear signal by applying little distortion
            // m_sound_data1[i] += 0.7f*sin(1000.*float(i) *2.f*3.14159*1./current_sample_rate);
            // if (m_sound_data1[i] > 0.f) m_sound_data1[i] = powf(m_sound_data1[i], 1.1f);
            // if (m_sound_data1[i] < 0.f) m_sound_data1[i] = -powf(-m_sound_data1[i], 1.1f);
            if(channelcount>1) m_sound_data2[i] = m_raw_buffer[i*channelcount+1] * m_audio_gain;
            if (m_fft_channel == 0){
                m_fftin[i] = m_sound_data1[i] * m_window_fn(i, m_capture_size);
            } else {
                m_fftin[i] = m_sound_data2[i] * m_window_fn(i, m_capture_size);
            }
            m_sound_data_x[i] = float(i) * inv_current_sample_rate * 1000.f;
        }
        
        if (compute_fft){
            // Compute and fill audio FFT
            fftwf_execute(m_fftplan);
            float sum = 0;
            for (int i = 0; i < fft_capture_size; ++i){
                m_fftfreqs[i] = current_sample_rate * 0.5f * inv_fft_capture_size * (float)(i); 
                float fftout = sqrtf(m_fftout[i][0] * m_fftout[i][0] + m_fftout[i][1] * m_fftout[i][1]) * inv_fft_capture_size;
                fftout = std::max(20.f * log10(fftout), -200.f);
                m_fftdraw[i] = isnan(fftout) ? -200.f : fftout;
                sum += fftout;
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

        smoothed_z_score(m_fftdraw, m_fftfiltered, fft_capture_size, 100, 4, 0.f);
        int one_count = 0;
        int found = 0;

        for(int i = 200; i < fft_capture_size; ++i){
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
        m_thd = 0;
        float fundamental_db = m_fftdraw[m_fft_highest_idx[fundamental_idx]];

        for (int i = fundamental_idx + 1; i < m_fft_found_peaks; ++i){
            float dBc = m_fftdraw[m_fft_highest_idx[i]] - fundamental_db;
            float v_rms = powf(10.f, dBc/20.f);
            m_thd += (v_rms * v_rms) ; 
        }

        m_thd = sqrtf(m_thd) * 100.f;
    }

    void draw_tools_windows(){
        if (m_sound_setup_open && !m_sweep_started){
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
                const std::vector<std::string> out_samplerate = m_audiomanager.get_output_sample_rates_str(m_audio_out_idx);
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
                const std::vector<std::string> in_samplerate = m_audiomanager.get_input_sample_rates_str(m_audio_in_idx);
                if (ImGui::Combo("Samplerate##2", &m_in_sample_rate, vector_getter, (void*)&in_samplerate, in_samplerate.size())){
                    reinit_recorder();
                }
                ImGui::PopItemWidth();
            }
            ImGui::End();
        }

        if (m_tone_generator_open && !m_sweep_started){
            if(ImGui::Begin("Tone Generator", &m_tone_generator_open)){
                if (ImGui::ToggleButton("Sine", &m_sine_generator_switch)){
                    reset_sine_generator();
                }
                ImGui::SameLine();
                if(ImGui::SliderFloat("Pitch", &m_pitch, 100, 20000)){
                    m_sine_generator.setPitch(m_pitch);
                }
                ImGui::End();
            }
        }
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
    App_SDL* app = App_SDL::get();
    Window_SDL* window = new MainWindow;
    ImGui::GetStyle().FrameRounding = 5.0;
    ImGui::GetStyle().ChildRounding = 5.0;
    ImGui::GetStyle().WindowRounding= 4.0;
    ImGui::GetStyle().GrabRounding = 4.0;
    ImGui::GetStyle().GrabMinSize = 4.0; 
    window->set_minimum_window_size(800,600);
    app->add_window(window);
    app->run();
    return 0;
}