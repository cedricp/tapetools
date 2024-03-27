#include "SDL.h"
#include "window_sdl.h"
#include "audio_sine_gen.h"
#include "audio_record.h"
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
    std::vector<float> m_sound_data;
    std::vector<float> m_sound_data_x;
    fftw_plan m_fftplan;
    double *m_fftin = NULL;
    fftw_complex *m_fftout = NULL;
    float *m_fftdraw = NULL;
    float *m_fftfreqs = NULL;
    int m_capture_size = 0;
    float m_audio_gain = 1.0f;
    int m_combo_in = 0;
    int m_combo_out = 0;

    int m_in_sample_rate = 0;
    int m_out_sample_rate = 0;

    float m_latency = 0.2f;
public:
    AudioToolWindow(Window_SDL* win) : Widget(win, "AudioTools"), m_audiorecorder(m_audiomanager)
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

        m_sine_generator.init(m_audiomanager, m_audio_out_idx, -1);
        reinit_recorder();
        init_capture(4096);
        m_fftplan = fftw_plan_dft_r2c_1d(m_capture_size, m_fftin, m_fftout, FFTW_ESTIMATE);
    }

    virtual ~AudioToolWindow(){
        m_sine_generator.destroy();
        fftw_destroy_plan(m_fftplan);

        delete[] m_fftin;
        delete[] m_fftout;
        delete[] m_fftdraw;
        delete[] m_fftfreqs;
    }

    void reinit_recorder()
    {
        if (m_audiorecorder.init(m_latency, m_audio_in_idx, m_audiomanager.get_input_sample_rates()[m_in_sample_rate])){
            m_audiorecorder.start();
        }
    }

    void init_capture(int capture_size)
    {
        m_fftin = new double[capture_size];
        m_fftout = new fftw_complex[capture_size];
        m_fftdraw = new float[capture_size/2];
        m_fftfreqs = new float[capture_size/2];   
        m_capture_size = capture_size;     
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
        int fft_capture_size = m_capture_size / 2;

        if (ImGui::ToggleButton("Sine", &m_sine_generator_switch)){
            reset_sine_generator();
        }

        if(ImGui::SliderFloat("pitch", &m_pitch, 100, 20000)){
            m_sine_generator.setPitch(m_pitch);
        }
        ImGui::SameLine();


        const std::vector<std::string>& out_devices = m_audiomanager.get_output_devices();
        if (ImGui::Combo("Ouput devs", &m_combo_out, vector_getter, (void*)&out_devices, out_devices.size())){
            reset_sine_generator();
        }

        //ImGui::SameLine();

        const std::vector<std::string>& in_devices = m_audiomanager.get_input_devices();
        if (ImGui::Combo("Input devs", &m_combo_in, vector_getter, (void*)&in_devices, in_devices.size())){
            m_audio_in_idx = m_audiomanager.get_input_device_map(m_combo_in);
            reinit_recorder();
        }

        const std::vector<std::string> out_samplerate = m_audiomanager.get_output_sample_rates_str();
        if (ImGui::Combo("Output samplerate", &m_out_sample_rate, vector_getter, (void*)&out_samplerate, out_samplerate.size())){
            reset_sine_generator();
        }

        const std::vector<std::string> in_samplerate = m_audiomanager.get_input_sample_rates_str();
        if (ImGui::Combo("Input samplerate", &m_in_sample_rate, vector_getter, (void*)&in_samplerate, in_samplerate.size())){

        }

        ImGui::SliderFloat("Gain", &m_audio_gain, 1.f, 100.f);
        
        if (m_audiorecorder.get_available_bytes() > m_capture_size){
            m_audiorecorder.get_data(m_sound_data, m_capture_size);
            m_sound_data_x.resize(m_capture_size);

            // Fill audio waveform
            for (int i = 0; i < m_capture_size; ++i){
                if (m_audio_gain != 1.0f){
                    m_sound_data[i] *= m_audio_gain;
                }
                m_fftin[i] = m_sound_data[i];
                m_sound_data_x[i] = float(i) / (current_sample_rate * 0.001f);
            }
            
            // Compute and fill audio FFT
            fftw_execute(m_fftplan);
            for (int i = 0; i < fft_capture_size; ++i){
                m_fftfreqs[i] = current_sample_rate / 2. / float(fft_capture_size) * float(i); 
                m_fftout[i][0] *= 2./float(m_capture_size);
                m_fftout[i][1] *= 2./float(m_capture_size);
                m_fftdraw[i] = m_fftout[i][0] * m_fftout[i][0] + m_fftout[i][1] * m_fftout[i][1];
                m_fftdraw[i] = 10. * log10(m_fftdraw[i]);
                if (isnan(m_fftdraw[i])){m_fftdraw[i] = 0;}
            }
        }

        float xmax = current_sample_rate > 0 ? float(m_capture_size) * (1.f/(current_sample_rate*0.001f)) : INFINITY;
        ImPlot::BeginPlot("Audio");
        ImPlot::SetupAxes("Time (ms)", "Amplitude", 0, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxesLimits(0, xmax, -1.0f, 1.0f);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, xmax);
        ImPlot::PlotLine("Audio samples", m_sound_data_x.data(), m_sound_data.data(), m_sound_data_x.size());
        ImPlot::EndPlot();

        float xfftmax = current_sample_rate > 0 ? (current_sample_rate)/2.f : INFINITY;
        ImPlot::BeginPlot("AudioFFT");
        ImPlot::SetupAxes("Frequency", "dB(FS)", 0, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxesLimits(0, xfftmax, -100, 0.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, xfftmax);
        ImPlot::PlotLine("Audio FFT", m_fftfreqs, m_fftdraw, m_sound_data_x.size()/2);
        ImPlot::EndPlot();
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