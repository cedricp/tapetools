#include "SDL.h"
#include "window_sdl.h"
#include "audio_sine_gen.h"
#include "audio_record.h"
#include "utils.h"
#include <fftw3.h>

class AudioToolWindow : public Widget
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
    
    float   (*m_window_fn)(int, int);
    int     m_fft_window_fn = 5;
    int     m_fft_channel = 0;
    float   m_noise_foor = -100;
    float   m_fft_highest_pos[200];
    int     m_fft_highest_idx[200];
    float   m_fft_highest_val;
    int     m_fft_found_peaks = 0;
    float   m_thd = 0; 
public:
    AudioToolWindow(Window_SDL* win) : Widget(win, "AudioTools"), m_audiorecorder(m_audiomanager)
    {
        set_maximized(true);
        set_movable(false);
        set_resizable(false);
        set_titlebar(false);

        m_window_fn = hann_fft_window;

        m_audiomanager.flush();

        m_audio_out_idx = m_audiomanager.get_default_output_device_id();
        m_audio_in_idx = m_audiomanager.get_default_input_device_id();

        m_combo_in = m_audiomanager.get_input_device_reverse_map(m_audio_in_idx);
        m_combo_out = m_audiomanager.get_output_device_reverse_map(m_audio_out_idx);

        m_sine_generator.init(m_audiomanager, m_audio_out_idx, -1, 0.2f);

        reinit_recorder();
    }

    virtual ~AudioToolWindow(){
        m_sine_generator.destroy();
        destroy_capture();
    }

    void destroy_capture()
    {
        delete[] m_fftin;
        delete[] m_fftout;
        delete[] m_fftdraw;
        delete[] m_fftfreqs;
        delete[] m_fftfiltered;

        if (m_fftplan) fftwf_destroy_plan(m_fftplan);
        m_fftin = nullptr;
        m_fftout = nullptr;
        m_fftdraw = nullptr;
        m_fftfreqs = nullptr;
        m_fftfiltered = nullptr;
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
        if (m_audiorecorder.init(m_latency, m_audio_in_idx, m_audiomanager.get_input_sample_rates()[m_in_sample_rate])){
            m_audiorecorder.start();
        }
        init_capture();
    }

    void reset_sine_generator(){
        int current_sine_samplerate = m_audiomanager.get_output_sample_rates()[m_out_sample_rate];
        m_sine_generator.destroy();
        m_sine_generator.init(m_audiomanager, m_audio_out_idx, current_sine_samplerate, 0.1f);
        m_sine_generator.start();
        m_sine_generator.pause(!m_sine_generator_switch);
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
            if (ImGui::BeginMenu("FFT")){
                if (ImGui::BeginMenu("Window")){
                    if (ImGui::MenuItem("Rectangle",       NULL, m_fft_window_fn == 0)){ m_fft_window_fn = 0;m_window_fn = rectangle_fft_window; }
                    if (ImGui::MenuItem("Hamming",         NULL, m_fft_window_fn == 1)){ m_fft_window_fn = 1;m_window_fn = hamming_fft_window; }
                    if (ImGui::MenuItem("Hann-Poisson",    NULL, m_fft_window_fn == 2)){ m_fft_window_fn = 2;m_window_fn = hann_poisson_fft_window; }
                    if (ImGui::MenuItem("Blackman",        NULL, m_fft_window_fn == 3)){ m_fft_window_fn = 3;m_window_fn = blackman_fft_window; }
                    if (ImGui::MenuItem("Blackman-Harris", NULL, m_fft_window_fn == 4)){ m_fft_window_fn = 4;m_window_fn = blackman_harris_fft_window; }
                    if (ImGui::MenuItem("Hann",            NULL, m_fft_window_fn == 5)){ m_fft_window_fn = 5;m_window_fn = hann_fft_window; }
                    ImGui::EndMenu();
                }
                if(ImGui::BeginMenu("Channel")){
                    if (ImGui::MenuItem("Left", NULL, m_fft_channel == 0)){ m_fft_channel = 0;}
                    if (channelcount>1 && ImGui::MenuItem("Right", NULL, m_fft_channel == 1)){ m_fft_channel = 1;}
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::BeginTabBar("MaintabBar");
        if (ImGui::BeginTabItem("Realtime analysis")){
            draw_rt_analysis();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sweep measurement")){

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();

        draw_tools_windows();
    }

    void draw_rt_analysis(){
        int channelcount = m_audiorecorder.get_channel_count(); 
        float current_sample_rate = m_audiorecorder.get_current_samplerate();

        compute();
        compute_thd();
        
        ImGui::BeginChild("ScopesChild", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
            int plotheight = height() / 2 - 5;
            if (ImPlot::BeginPlot("Audio", ImVec2(-1, plotheight))){
                float xmax = current_sample_rate > 0 ? float(m_capture_size) * (1.f/(current_sample_rate*0.001f)) : INFINITY;
                ImPlot::SetupAxes("Time (ms)", "Amplitude", 0, ImPlotAxisFlags_Lock);
                ImPlot::SetupAxesLimits(0, xmax, -1.f, 1.f);
                ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, xmax);
                if (channelcount>0) ImPlot::PlotLine("Channel 1", m_sound_data_x.data(), m_sound_data1.data(), m_sound_data_x.size());
                if (channelcount>1) ImPlot::PlotLine("Channel 2", m_sound_data_x.data(), m_sound_data2.data(), m_sound_data_x.size());
                ImPlot::EndPlot();
            }

            if (ImPlot::BeginPlot("AudioFFT", ImVec2(-1, plotheight))){
                float xfftmax = current_sample_rate > 0 ? (current_sample_rate)/2.f : INFINITY;
                ImPlot::SetupAxes("Frequency", "dB FullScale", 0, ImPlotAxisFlags_Lock);
                ImPlot::SetupAxesLimits(0, xfftmax, -130, 0.0);
                ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, xfftmax);
                ImPlotRect selection = ImPlot::GetPlotLimits(IMPLOT_AUTO);
                char thdtext[16];
                snprintf(thdtext, 16, "THD : %.2f %%", m_thd);
                ImPlot::PlotText(thdtext, selection.Min().x + (selection.Max().x - selection.Min().x)/2.0, -10.0f);
                if (channelcount>0) ImPlot::PlotLine("Audio FFT", m_fftfreqs, m_fftdraw, m_sound_data_x.size()/2);
                for (int i = 0; i < m_fft_found_peaks; ++i){
                    float fund[4] = {m_fft_highest_pos[i], m_fft_highest_pos[i], 0., -130.};
                    ImPlot::PlotLine("Peaks", fund, fund+2, 2);
                }
                float nf[4] = {0., (current_sample_rate)/2.f, m_noise_foor, m_noise_foor};
                ImPlot::PlotLine("Noise floor", nf, nf+2, 2);
                ImPlot::EndPlot();
            }
            ImGui::EndChild();
    }

    void compute(bool compute_fft = true, bool compute_noise_floor = true){
        int channelcount = m_audiorecorder.get_channel_count();

        if (channelcount == 0 || m_audiorecorder.get_available_samples() < m_capture_size * channelcount){
            return;
        }

        float current_sample_rate = m_audiorecorder.get_current_samplerate();
        float inv_current_sample_rage = 1.0f / current_sample_rate;
        const int fft_capture_size = m_capture_size / 2;
        const float inv_fft_capture_size = 1.0f / float(fft_capture_size);
        m_fft_highest_val = -100;
        std::vector<float> raw_buffer;
        m_sound_data1.resize(m_capture_size);
        m_sound_data2.resize(m_capture_size);
        m_audiorecorder.get_data(raw_buffer, m_capture_size * channelcount);
        m_sound_data_x.resize(m_capture_size);

        // Fill audio waveform
        for (int i = 0; i < m_capture_size; i++){
            m_sound_data1[i] = raw_buffer[i*channelcount] * m_audio_gain;
            // THD test for non linear signal by applying little distortion
            m_sound_data1[i] += 0.7f*sin(1000.*float(i) *2.f*3.14159*1./current_sample_rate);
            if (m_sound_data1[i] > 0.f) m_sound_data1[i] = powf(m_sound_data1[i], 1.1f);
            if (m_sound_data1[i] < 0.f) m_sound_data1[i] = -powf(-m_sound_data1[i], 1.1f);
            if(channelcount>1) m_sound_data2[i] = raw_buffer[i*channelcount+1] * m_audio_gain;
            if (m_fft_channel == 0){
                m_fftin[i] = m_sound_data1[i] * m_window_fn(i, m_capture_size);
            } else {
                m_fftin[i] = m_sound_data2[i] * m_window_fn(i, m_capture_size);
            }
            m_sound_data_x[i] = float(i) * inv_current_sample_rage * 1000.f;
        }
        
        if (compute_fft){
            // Compute and fill audio FFT
            fftwf_execute(m_fftplan);
            float sum = 0;
            for (int i = 0; i < fft_capture_size; ++i){
                m_fftfreqs[i] = current_sample_rate * 0.5f * inv_fft_capture_size * float(i); 
                float fftout = sqrtf(m_fftout[i][0] * m_fftout[i][0] + m_fftout[i][1] * m_fftout[i][1]) * inv_fft_capture_size;
                fftout = std::max(20.f * log10(fftout), -200.f);
                m_fftdraw[i] = isnan(fftout) ? -200.f : fftout;
                sum += fftout;
            }


            if (compute_noise_floor){
                // Compute noise floor
                float mean = sum / fft_capture_size;
                float stddev = 0;
                for (int i = 0; i < fft_capture_size; ++i){
                    float a = (m_fftdraw[i] - mean);
                    stddev += a * a;
                }
                stddev = sqrtf(stddev / (fft_capture_size - 1));
                m_noise_foor = mean + stddev;
            } // compute_noise_floor
        } // compute_fft
    }

    void compute_thd(){
        // Find peaks
        // Source : https://stackoverflow.com/questions/22583391/peak-signal-detection-in-realtime-timeseries-data
        const int fft_capture_size = m_capture_size / 2;

        smoothed_z_score(m_fftdraw, m_fftfiltered, fft_capture_size, 50, 4, 0.f);
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
            if(one_count && current_sample < 1.f){
                int freq_start = i - one_count;
                float max = -130;
                int freq_idx = freq_start;
                // Find max value in range
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
        float fundamental = powf(10.f, fundamental_db/20.f);
        for (int i = fundamental_idx + 1; i < m_fft_found_peaks; ++i){
            float dbc = m_fftdraw[m_fft_highest_idx[i]] - fundamental_db;
            float v = powf(10.f, dbc/20.f);
            m_thd += (v * v) ; 
        }
        m_thd = sqrtf(m_thd) / fundamental * 100.f;
    }

    void draw_tools_windows(){
        if (m_sound_setup_open && ImGui::Begin("Sound card setup", &m_sound_setup_open)){
            ImVec2 winsize = ImGui::GetWindowSize();
            ImGui::PushItemWidth(winsize.x / 3);
            const std::vector<std::string>& out_devices = m_audiomanager.get_output_devices();
            ImGui::SeparatorText("Output device");
            if (ImGui::Combo("Ouput", &m_combo_out, vector_getter, (void*)&out_devices, out_devices.size())){
                m_audio_out_idx = m_audiomanager.get_output_device_map(m_combo_out);
                reset_sine_generator();
            }
            ImGui::SameLine();
            const std::vector<std::string> out_samplerate = m_audiomanager.get_output_sample_rates_str();
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
            const std::vector<std::string> in_samplerate = m_audiomanager.get_input_sample_rates_str();
            if (ImGui::Combo("Samplerate##2", &m_in_sample_rate, vector_getter, (void*)&in_samplerate, in_samplerate.size())){
                reinit_recorder();
            }
            ImGui::PopItemWidth();
            ImGui::End();
        }

        if (m_tone_generator_open && ImGui::Begin("Tone Generator", &m_tone_generator_open)){
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