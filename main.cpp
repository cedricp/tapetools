#include "SDL.h"
#include "window_sdl.h"
#include "audio_sine_gen.h"
#include "audio_record.h"
#include <fftw3.h>

double rectangle(int i, int length)
{
	return 1.0;
}

double hamming(int i, int length)
{
	double a, b, w, N1;
	a = 25.0/46.0;
	b = 21.0/46.0;
	N1 = (double)(length-1);
	w = a - b*cos(2*i*M_PI/N1);
	return w;
}

double hann_poisson(int i, int length)
{
	double a, N1, w;
	a = 2.0;
	N1 = (double)(length-1);
	w = 0.5 * (1 - cos(2*M_PI*i/N1)) * \
	    pow(M_E, (-a*(double)abs((int)(N1-1-2*i)))/N1);
	return w;
}

double blackman(int i, int length)
{
	double a0, a1, a2, w, N1;
	a0 = 7938.0/18608.0;
	a1 = 9240.0/18608.0;
	a2 = 1430.0/18608.0;
	N1 = (double)(length-1);
	w = a0 - a1*cos(2*i*M_PI/N1) + a2*cos(4*i*M_PI/N1);
	return w;
}

double blackman_harris(int i, int length)
{
	double a0, a1, a2, a3, w, N1;
	a0 = 0.35875;
	a1 = 0.48829;
	a2 = 0.14128;
	a3 = 0.01168;
	N1 = (double)(length-1);
	w = a0 - a1*cos(2*i*M_PI/N1) + a2*cos(4*i*M_PI/N1) - a3*cos(6*i*M_PI/N1);
	return w;
}

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
    fftw_plan m_fftplan = NULL;
    double *m_fftin = nullptr;
    fftw_complex *m_fftout = nullptr;
    float *m_fftdraw = nullptr;
    float *m_fftfreqs = nullptr;
    int m_capture_size = 0;
    float m_audio_gain = 1.0f;
    int m_combo_in = 0;
    int m_combo_out = 0;
    int m_in_sample_rate = 0;
    int m_out_sample_rate = 0;
    float m_latency = 0.2f;

    bool m_sound_setup_open = false;
    bool m_tone_generator_open = false;
    double (*m_window_fn)(int, int);
    
public:
    AudioToolWindow(Window_SDL* win) : Widget(win, "AudioTools"), m_audiorecorder(m_audiomanager)
    {
        set_maximized(true);
        set_movable(false);
        set_resizable(false);
        set_titlebar(false);

        m_window_fn = hamming;

        m_audiomanager.flush();

        m_audio_out_idx = m_audiomanager.get_default_output_device_id();
        m_audio_in_idx = m_audiomanager.get_default_input_device_id();

        m_combo_in = m_audiomanager.get_input_device_reverse_map(m_audio_in_idx);
        m_combo_out = m_audiomanager.get_output_device_reverse_map(m_audio_out_idx);

        m_sine_generator.init(m_audiomanager, m_audio_out_idx, -1);

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

        if (m_fftplan) fftw_destroy_plan(m_fftplan);
        m_fftin = nullptr;
        m_fftout = nullptr;
        m_fftdraw = nullptr;
        m_fftfreqs = nullptr;
    }

    void init_capture()
    {
        int capture_size = m_audiorecorder.get_buffer_capacity(m_latency);
        if (capture_size == 0){
            return;
        }
        destroy_capture(); 
        m_fftin = new double[capture_size];
        m_fftout = new fftw_complex[capture_size];
        m_fftdraw = new float[capture_size/2];
        m_fftfreqs = new float[capture_size/2];   
        m_capture_size = capture_size;
        m_fftplan = fftw_plan_dft_r2c_1d(m_capture_size, m_fftin, m_fftout, FFTW_MEASURE);
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
        m_sine_generator.init(m_audiomanager, m_audio_out_idx, current_sine_samplerate);
        m_sine_generator.start();
        m_sine_generator.pause(!m_sine_generator_switch);
    }

    void draw() override {
        m_audiomanager.flush();
        float current_sample_rate = m_audiorecorder.get_current_samplerate();

        // Check that the buffer contains enough data
        int channelcount = m_audiorecorder.get_channel_count();
        int channel_capture_count = channelcount == 0 ? -1 : m_capture_size /  channelcount;
        const int fft_capture_size = channel_capture_count / 2;
        const float inv_capture_size = 1.0f / float(fft_capture_size);

        if (m_audiorecorder.get_available_samples() >= channel_capture_count){
            std::vector<float> raw_buffer;
            m_sound_data1.resize(channel_capture_count);
            m_sound_data2.resize(channel_capture_count);
            m_audiorecorder.get_data(raw_buffer, m_capture_size);
            m_sound_data_x.resize(channel_capture_count);

            // Fill audio waveform
            for (int i = 0; i < channel_capture_count; i++){
                m_sound_data1[i] = raw_buffer[i*channelcount] * m_audio_gain;
                if(channelcount>1) m_sound_data2[i] = raw_buffer[i*channelcount+1] * m_audio_gain;
                m_fftin[i] = m_sound_data1[i] * m_window_fn(i, m_capture_size);
                m_sound_data_x[i] = float(i) / (current_sample_rate * 0.001f);
            }
            
            // Compute and fill audio FFT
            fftw_execute(m_fftplan);
            for (int i = 0; i < fft_capture_size; ++i){
                m_fftfreqs[i] = current_sample_rate * 0.5f * inv_capture_size * float(i); 
                float fftout = sqrtf(m_fftout[i][0] * m_fftout[i][0] + m_fftout[i][1] * m_fftout[i][1]) * inv_capture_size;
                fftout = 20.f * log10(fftout);
                m_fftdraw[i] = std::max(fftout, -100.f);
            }
        }
        
        
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Preferences")){
                ImGui::MenuItem("Sound card setup", nullptr, &m_sound_setup_open);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Tools")){
                ImGui::MenuItem("Tone generator", nullptr, &m_tone_generator_open);
                ImGui::EndMenu();
            }
            static int location = 0;
            if (ImGui::BeginMenu("FFT window")){
                if (ImGui::MenuItem("Hamming",         NULL, location == 0)){ location = 0;m_window_fn = hamming; }
                if (ImGui::MenuItem("Rectangle",       NULL, location == 1)){ location = 1;m_window_fn = rectangle; }
                if (ImGui::MenuItem("Hann-Poisson",    NULL, location == 2)){ location = 2;m_window_fn = hann_poisson; }
                if (ImGui::MenuItem("Blackman",        NULL, location == 3)){ location = 3;m_window_fn = blackman; }
                if (ImGui::MenuItem("Blackman-Harris", NULL, location == 4)){ location = 4;m_window_fn = blackman_harris; }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::BeginChild("ScopesChild", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
        int plotheight = height() / 2 - 5;
        if (ImPlot::BeginPlot("Audio", ImVec2(-1, plotheight))){
            float xmax = current_sample_rate > 0 ? float(channel_capture_count) * (1.f/(current_sample_rate*0.001f)) : INFINITY;
            ImPlot::SetupAxes("Time (ms)", "Amplitude", 0, ImPlotAxisFlags_Lock);
            ImPlot::SetupAxesLimits(0, xmax, -1.f, 1.f);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, xmax);
            ImPlot::PlotLine("Channel 1", m_sound_data_x.data(), m_sound_data1.data(), m_sound_data_x.size());
            if (channelcount>1) ImPlot::PlotLine("Channel 2", m_sound_data_x.data(), m_sound_data2.data(), m_sound_data_x.size());
            ImPlot::EndPlot();
        }

        if (ImPlot::BeginPlot("AudioFFT", ImVec2(-1, plotheight))){
            float xfftmax = current_sample_rate > 0 ? (current_sample_rate)/2.f : INFINITY;
            ImPlot::SetupAxes("Frequency", "dB FullScale", 0, ImPlotAxisFlags_Lock);
            ImPlot::SetupAxesLimits(0, xfftmax, -100, 0.0);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, xfftmax);
            ImPlot::PlotLine("Audio FFT", m_fftfreqs, m_fftdraw, m_sound_data_x.size()/2);
            ImPlot::EndPlot();
        }
        ImGui::EndChild();


        if (ImGui::CollapsingHeader("Input setup")){
            ImGui::SliderFloat("Gain", &m_audio_gain, 1.f, 100.f);
            ImGui::Separator();
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