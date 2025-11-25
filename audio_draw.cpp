#define _USE_MATH_DEFINES
#include "main_widget.h"
#include "lcd_display.h"
#include <spline.h>

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

bool manage_slider_mousewheel_int(int &val, int min, int max, int step = 1)
{
    bool shiftdown = ImGui::IsKeyDown(ImGuiKey_ModShift);
    bool ctrldown  = ImGui::IsKeyDown(ImGuiKey_ModCtrl);

    int range = max-min;

    if (shiftdown && ctrldown)
    {
        step *= range / 5;
    }
    else if (shiftdown)
    {
        step *= range / 10;
    }
    else if (ctrldown)
    {
        step *= range / 100;
    }

    ImGui::SetItemUsingMouseWheel();
    if( ImGui::IsItemHovered() )
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if( wheel )
        {
            if( ImGui::IsItemActive() )
            {
                ImGui::ClearActiveID();
            }
            else
            {
                val += step * wheel;
            }
        }
        else {
            return false;
        }
        if (val > max) val = max;
        if (val < min) val = min;
        return true;
    }
    return false;
}

void interpolate_linear_to_hspline_curve(const std::vector<double> &freqs, const std::vector<double> &vals,
                            std::vector<double> &xspline, std::vector<double> &yspline)
{
    const int numpnts = xspline.size();
    tk::spline spline(freqs, vals, tk::spline::cspline_hermite);

    double f_min = freqs.front();
    double f_max = freqs.back();
    // We want to draw the spline in log scale
    double logfreq_min = log10(f_min);
    double logfreq_max = log10(f_max);
    double step = (logfreq_max - logfreq_min) / numpnts;
    double curr_log_freq = logfreq_min;

    for (int i = 0; i < numpnts; ++i)
    {
        double f_new = pow(10, curr_log_freq);
        xspline[i] = f_new;
        yspline[i] = spline(f_new);
        curr_log_freq += step;
    }
}


void AudioToolWindow::draw_sweep_tab()
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
                m_sweep_time = m_measure_delay;
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

    if (!m_sweep_values.empty() && !m_sweep_started){
        static char buffer[64] = "Memory";
        int i = 1;
        while (std::find(m_mem_sweeps_names.begin(), m_mem_sweeps_names.end(), buffer) != m_mem_sweeps_names.end())
        {
            snprintf(buffer, 64, "Memory #%i", i++);
        }
        ImGui::SameLine();
        ImGui::BeginChild("MemoChild", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::AlignTextToFramePadding();
        if (ImGui::Button("Memorize this graph"))
        {
            if (std::find(m_mem_sweeps_names.begin(), m_mem_sweeps_names.end(), buffer) == m_mem_sweeps_names.end())
            {
                std::vector<double> xspline(400), yspline(400);
                interpolate_linear_to_hspline_curve(m_sweep_freqs, m_sweep_values, xspline, yspline);
                m_mem_sweeps_results.push_back(std::pair<std::vector<double>, std::vector<double>>(xspline, yspline));
                m_mem_sweeps_names.push_back(buffer);
                m_sweep_freqs.clear();
                m_sweep_values.clear();
            }
        }
        ImGui::SameLine();
        ImGui::InputText("Curve name", buffer, 64);
        ImGui::EndChild();
    }

    draw_tone_generator_widget();

    ImGui::BeginChild("PlotChild", ImVec2(-1, height()), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
    if (ImPlot::BeginPlot("AudioFFT", ImVec2(-1, -1), ImPlotFlags_NoCentralMenu | ImPlotFlags_Crosshairs))
    {
        if (ImPlot::BeginCustomContext())
        {
            for (int i = 0; i < m_mem_sweeps_names.size(); ++i)
            {
                std::string item_string = "Remove curve " + m_mem_sweeps_names[i];
                if (ImGui::MenuItem(item_string.c_str()))
                {
                    m_mem_sweeps_names.erase(m_mem_sweeps_names.begin() + i);
                    m_mem_sweeps_results.erase(m_mem_sweeps_results.begin() + i);
                    break;
                }
            }
            ImPlot::EndCustomContext(false);
        }
        bool calibration_active = m_rms_calibration_scale != 1.0;
        float xfftmax = current_sample_rate > 0 ? (current_sample_rate)/2.f : INFINITY;
        ImPlot::SetupAxes("Frequency", "dB FullScale", 0, ImPlotAxisFlags_Lock | ImPlotAxisFlags_Opposite);
        if (m_logscale_frequency){
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        }
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
        ImPlot::SetupAxesLimits(0.f, xfftmax, -120.0, 0.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 20.0, 24000.0);

        if (calibration_active)
        {
            double diffdb = 20.0 * log10(m_rms_calibration_scale);
            ImPlot::SetupAxis(ImAxis_Y2, "dBu", 0);
            ImPlot::SetupAxisLimits(ImAxis_Y2, -120 + diffdb, diffdb, ImPlotCond_Always);
        }

        if (channelcount>0 && m_fftfreqs)
        {
            ImPlot::PlotLine("Audio FFT", m_fftfreqs, m_fft_channel_left  ?  m_fftdrawl : m_fftdrawr, m_sound_data_x.size()/2);
            double nf[4] = {0., (current_sample_rate)/2.0, m_noise_foor, m_noise_foor};
            ImPlot::PlotLine("Noise floor", nf, nf+2, 2);
            if (m_sweep_freqs.size() > 3){
                std::vector<double> xspline(400), yspline(400);
                interpolate_linear_to_hspline_curve(m_sweep_freqs, m_sweep_values, xspline, yspline);
                ImPlot::PlotLine("Frequency response", xspline.data(), yspline.data(), 400);
            }
        }

        int i = 0;
        for (auto sweep_result: m_mem_sweeps_results)   
        {
            ImPlot::PlotLine(m_mem_sweeps_names[i++].c_str(), sweep_result.first.data(), sweep_result.second.data(), sweep_result.second.size());
        }

        float sweep_bar[4] = {(float)m_sweep_last_measure_freq, (float)m_sweep_last_measure_freq, 40.0, -200.0};
        ImPlot::PlotLine("Sweep", sweep_bar, sweep_bar+2, 2);
        
        ImPlot::EndPlot();
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

void AudioToolWindow::draw_lcd(const float value, const ImVec2 size, const int lcd_digits_size)
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

void AudioToolWindow::draw_audio_time_domain_widget(int plotheight, int current_sample_rate, int channelcount)
{
    if (ImPlot::BeginPlot("Audio", ImVec2(m_show_wow_flutter ? width()-plotheight*2-10 : -1, -1)))
    {
        bool triggered = false;
        const int trigger_space = m_capture_size / 10;
        int computed_capture_size = m_capture_size;
        if (m_trigger_on && m_trigger_index > 0 && m_trigger_index < trigger_space)
        {
            computed_capture_size -= trigger_space;
            triggered = true;
        }

        double x_limit = 1.0f / m_scopezoom;
        double xmax = current_sample_rate > 0 ? float(computed_capture_size) * (1.0 / (current_sample_rate * 0.001)) : INFINITY;
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
        double* left_data = m_sound_data1.data() + (triggered ? m_trigger_index : 0);
        double* right_data = m_sound_data2.data() + (triggered ? m_trigger_index : 0);
        
        if (channelcount > 0) ImPlot::PlotLine("Left channel", m_sound_data_x.data(), left_data, m_sound_data_x.size());
        if (channelcount > 1) ImPlot::PlotLine("Right channel", m_sound_data_x.data(), right_data, m_sound_data_x.size());
        
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

        if (triggered)
        {
            ImVec2 plotpos = ImPlot::GetPlotPos();
            ImVec2 plotsize = ImPlot::GetPlotSize();
            ImPlotPoint pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.05)));
            ImPlot::PlotText("TRIGGERED", pnt.x, pnt.y);
        }
        ImPlot::EndPlot();
    }
}

void AudioToolWindow::draw_wow_flutter_widget(int channelcount, int current_sample_rate, int plotheight)
{
    const char* ref_freq_presets[] = {"3000","3150", "Custom"};
    const char* filter_presets[] = {"Disabled", "Wow (6Hz)","Flutter low (20Hz)", "Flutter high (100Hz)"};
    float ref_frequency;
    static bool iq_view = false;
    static float iq_separation = 0.f;
    
    ImGui::BeginChild("ChildWF", ImVec2(0, -1), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);

    ImGui::BeginChild("ChildFFTControl", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
    ImGui::ToggleButton("FFT view", &m_show_wf_fft_view);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("ChildWFControl", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiWindowFlags_None);
    ImGui::SetNextItemWidth(80);
    ImGui::Combo("Ref. frequency", &m_wow_test_frequency, ref_freq_presets, 3);
    ImGui::SetItemTooltip("Set reference frequency");
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
    ImGui::Combo("LPF (Hz)", &m_wf_filter_freq_combo, filter_presets, 4);
    ImGui::SetItemTooltip("Set low pass filter frequency");
    ImGui::EndChild();

    if (channelcount > 1)
    {
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildChannelSelect", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (ImGui::Checkbox("Left", &m_fft_channel_left) )
        {
            m_fft_channel_right = !m_fft_channel_left;
        }
        ImGui::SetItemTooltip("Analyse left channel");
        ImGui::SameLine();
        if (ImGui::Checkbox("right", &m_fft_channel_right) )
        {
            m_fft_channel_left = !m_fft_channel_right;
        }
        ImGui::SetItemTooltip("Analyse right channel");
        ImGui::EndChild();
    }

    if (!m_show_wf_fft_view && m_debug_info)
    {
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildIQDebug", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::ToggleButton("IQ view", &iq_view);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50);
        ImGui::SliderFloat("IQ separation", &iq_separation, 0.0, 1);
        ImGui::EndChild();
    }

    if (m_debug_info)
    {
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildDebug", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Thread time : %lums", m_wf_compute_time/1000);
        ImGui::EndChild();
    }
    static float max_freq = 100;
    static float max_fft_freq = 20;
    static float max_iq = 1.;

    if (m_wow_test_frequency == 0) ref_frequency = 3000;
    else if (m_wow_test_frequency == 1) ref_frequency = 3150;
    else if (m_wow_test_frequency == 2) ref_frequency = m_wow_test_frequency_custom;

    float max_percent = (max_freq / ref_frequency) * 100.;
    bool is_buffering = m_longterm_audio.size() < WOW_FLUTTER_ANALYSIS_TIME * m_audiorecorder.get_current_samplerate();

    if(!m_show_wf_fft_view && ImPlot::BeginPlot("Wow and flutter analysis (unweighted)", ImVec2(plotheight*2, -1)))
    {
        ImPlot::SetupAxes("Time (seconds)", "Freqency drift (Hz)", 0, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0., 5.);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0., 5., 0);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -max_freq, max_freq, ImPlotCond_Always);
        ImPlot::SetupAxis(ImAxis_Y2, "Peak drift %", ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_Y2, -max_percent, max_percent, ImPlotCond_Always);
        if (iq_view)
        {
            ImPlot::SetupAxis(ImAxis_Y3, "IQ", ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines);
            ImPlot::SetupAxisLimits(ImAxis_Y3, -max_iq, max_iq, ImPlotCond_Always);
            if (ImPlot::IsAxisHovered(ImAxis_Y3)){
                // Zoom Y axis in/out
                max_iq += ImGui::GetIO().MouseWheel * 0.05;
                if (max_iq < 0.1) max_iq = 0.1;
                if (max_iq > 2.0) max_iq = 2.0;
            }
        }


        if (ImPlot::IsAxisHovered(ImAxis_Y1) || ImPlot::IsAxisHovered(ImAxis_Y2)){
            // Zoom Y axis in/out
            max_freq += ImGui::GetIO().MouseWheel * -10;
            if (max_freq < 10) max_freq = 10;
            if (max_freq > 500) max_freq = 500;
        }
        
        m_wow_data_mutex.lock();
            ImPlot::SetAxis(ImAxis_Y1);
            ImPlot::PlotLine("Wow and flutter", m_wow_flutter_data_x.data(), m_wow_flutter_data.data(), m_wow_flutter_data.size());
            
            double wow_mean_bar[4] = {0., 5., m_wow_mean, m_wow_mean};
            ImPlot::PlotLine("Wow & flutter mean", wow_mean_bar, wow_mean_bar+2, 2);

            float peak_percent = (m_wow_peak_detection / (ref_frequency + m_wow_mean)) * 100.;
            float freq_drift = (m_wow_mean / ref_frequency) * 100.;
            if (!m_show_wf_fft_view && iq_view && m_signal_i.size() && m_signal_q.size())
            {
                std::pair<std::vector<double>, std::vector<double>> plotdatai(m_wow_flutter_data_x, m_signal_i);
                std::pair<std::vector<double>, std::vector<double>> plotdataq(m_wow_flutter_data_x, m_signal_q);
                
                auto offsetter1 = [](int idx, void* data) -> ImPlotPoint { 
                    int smp_idx = idx * WOW_FLUTTER_DECIMATION;
                    auto& slice = *(std::pair<std::vector<double>, std::vector<double>>*)data;
                    return ImPlotPoint(slice.first[idx], slice.second[smp_idx] + iq_separation);
                };
                auto offsetter2 = [](int idx, void* data) -> ImPlotPoint { 
                    int smp_idx = idx * WOW_FLUTTER_DECIMATION;
                    auto& slice = *(std::pair<std::vector<double>, std::vector<double>>*)data;
                    return ImPlotPoint(slice.first[idx], slice.second[smp_idx] - iq_separation);
                };

                ImPlot::SetAxis(ImAxis_Y3);
                ImPlot::PlotLineG("I", offsetter1, &plotdatai, m_wow_flutter_data_x.size());
                ImPlot::PlotLineG("Q", offsetter2, &plotdataq, m_wow_flutter_data_x.size());
                ImPlot::SetAxis(ImAxis_Y1);
            }
        m_wow_data_mutex.unlock();

        char peak_text[64];
        snprintf(peak_text, 32, "W&F Peak: %.3f %%", peak_percent);
        ImVec2 plotpos = ImPlot::GetPlotPos();
        ImVec2 plotsize = ImPlot::GetPlotSize();
        ImPlotPoint pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.05)));
        ImPlot::PlotText(peak_text, pnt.x, pnt.y);
        if (is_buffering)
        {
            snprintf(peak_text, 32, "Buffering...");
        } else
        {
            snprintf(peak_text, 32, "Frequency drift: %.3f %%", freq_drift);
        }
        pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.1)));
        ImPlot::PlotText(peak_text, pnt.x, pnt.y);
        ImPlot::EndPlot();
    }
    else
    if (ImPlot::BeginPlot("Wow and flutter FFT analysis", ImVec2(plotheight*2, -1)))
    {
        double max_frequency = current_sample_rate / WOW_FLUTTER_DECIMATION / 2.;
        if (m_wf_filter_freq_combo == 1) max_frequency = 10.;
        else if (m_wf_filter_freq_combo == 2) max_frequency = 25.;
        else if (m_wf_filter_freq_combo == 3) max_frequency = 110;


        const double drift_percent = max_fft_freq / ref_frequency * 100.;
        ImPlot::SetupAxes("Frequency (Hz)", "Freqency drift (Hz)", 0, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_SymLog);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0.2, max_frequency);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.2, max_frequency, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_fft_freq, ImPlotCond_Always);
        ImPlot::SetupAxis(ImAxis_Y2, "Drift %", ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_Y2, 0, drift_percent, ImPlotCond_Always);

        if (ImPlot::IsAxisHovered(ImAxis_Y1) || ImPlot::IsAxisHovered(ImAxis_Y2)){
            // Zoom Y axis in/out
            max_fft_freq += ImGui::GetIO().MouseWheel * -5;
            if (max_fft_freq < 5) max_fft_freq = 5;
            if (max_fft_freq > 500) max_fft_freq = 500;
        }

        m_wow_data_mutex.lock();
            ImPlot::PlotLine("Frequency drift", m_fftwowdrawfreqs.data(), m_fftdrawwow.data(), m_fftwowdrawfreqs.size());
        m_wow_data_mutex.unlock();

        char peak_text[64];
        double wow_zero = m_fftdrawwow.size() ? m_fftdrawwow[0] : 0;
        float percent_drift = wow_zero / ref_frequency * 100.f;
        if (is_buffering)
        {
            snprintf(peak_text, 32, "Buffering...");
        }
        else
        {
            snprintf(peak_text, 32, "Drift: [%.3f Hz] [%.3f %%]", wow_zero, percent_drift);
        }
        ImVec2 plotpos = ImPlot::GetPlotPos();
        ImVec2 plotsize = ImPlot::GetPlotSize();
        ImPlotPoint pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.1)));
        ImPlot::PlotText(peak_text, pnt.x, pnt.y);

        ImPlot::EndPlot();
    }
    ImGui::EndChild();
}

void AudioToolWindow::draw_voltmeter_widget(int channelcount)
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
        TextCenter("Frequency KHz");
        lcd_fg = IM_COL32(0,200,0,255);
        draw_lcd(m_frequency_counter / 1000., ImVec2(180, 60), 6);
        ImGui::EndChild();

        ImGui::EndChild();
        ImGui::SameLine();
}

void AudioToolWindow::draw_audio_fft_widget(int channelcount, int current_sample_rate, int plotheight)
{
    if (ImPlot::BeginPlot("Audio FFT", ImVec2(m_compute_channel_phase ? width() - plotheight * 1.5f - 10 : -1, -1), ImPlotFlags_Crosshairs))
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
        ImPlot::SetupAxesLimits(20.f, xfftmax, -120.0, 0.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 20.f, 24000.f);

        double diffdb = 0;
        if (calibration_active)
        {
            diffdb = 20.0 * log10(m_rms_calibration_scale);
            ImPlot::SetupAxis(ImAxis_Y2, "dBu", 0);
            ImPlot::SetupAxisLimits(ImAxis_Y2, -120 + diffdb, diffdb, ImPlotCond_Always);
        }

        if (m_fftfreqs && m_show_thd)
        {
            ImPlot::PlotShaded("Fundamental detection", (double*)&m_fftfreqs[m_fft_fund_idx_range_min+1], (double*)&fft_draw[m_fft_fund_idx_range_min+1], m_fft_fund_idx_range_max - m_fft_fund_idx_range_min, -200.0);
            
            float snr = fft_draw[m_fft_harmonics_idx[0]] - m_noise_foor;

            char thdtext[64];
            snprintf(thdtext, 32, "THD : %.3f%%", m_thd);
            ImVec2 plotpos = ImPlot::GetPlotPos();
            ImVec2 plotsize = ImPlot::GetPlotSize();
            float plot_to_pix_graph = 140. / plotsize.y;
            ImPlotPoint pnt = ImPlot::PixelsToPlot(ImVec2(plotpos.x + (plotsize.x*0.5), plotpos.y + (plotsize.y*0.05)));
            ImPlot::PlotText(thdtext, pnt.x, pnt.y);
            pnt.y -= 12 * plot_to_pix_graph;
            snprintf(thdtext, 32, "THD+N : %.3f %% (%.2fdB)", m_thdn, m_thddb);
            ImPlot::PlotText(thdtext, pnt.x, pnt.y);
            pnt.y -= 12 * plot_to_pix_graph;
            snprintf(thdtext, 32, "Total Vrms : %.4f  SNR : %.2fdB", m_fft_rms * m_rms_calibration_scale, snr);
            ImPlot::PlotText(thdtext, pnt.x, pnt.y);

            for (int i = 0; i < m_fft_found_peaks; ++i)
            {
                double fund[4] = {m_fft_harmonics_freq[i], m_fft_harmonics_freq[i], 40.0, -200.0};
                ImPlot::PlotLine("Peaks", fund, fund+2, 2);
                double y_pos = fft_draw[m_fft_harmonics_idx[i]];
                if (!calibration_active)
                {
                    snprintf(thdtext, 16, "%.4fdB", y_pos);
                }
                else
                {
                    double y_pos2 = y_pos + diffdb;
                    snprintf(thdtext, 16, "%.4fdBu", y_pos2);
                } 
                ImPlot::PlotText(thdtext, m_fft_harmonics_freq[i], y_pos+40 * plot_to_pix_graph);
                double freq = m_fftfreqs[m_fft_harmonics_idx[i]] / 1000.0;
                snprintf(thdtext, 16, "%.4fKHz", freq);
                ImPlot::PlotText(thdtext, m_fft_harmonics_freq[i], y_pos+20 * plot_to_pix_graph);
            }
        }

        double nf[4] = {0., (current_sample_rate)/2.0, m_noise_foor, m_noise_foor};
        ImPlot::PlotLine("Noise floor", nf, nf+2, 2);

        double* current_fft_draw = m_fft_channel_left ? m_fftdrawl : m_fftdrawr;
        
        if (channelcount>0 && m_fftfreqs)
        {
            if (m_fft_channel_left) ImPlot::PlotLine("Audio left FFT", m_fftfreqs, current_fft_draw, m_sound_data_x.size()/2);
            if (m_fft_channel_right) ImPlot::PlotLine("Audio right FFT", m_fftfreqs, current_fft_draw, m_sound_data_x.size()/2);
        }

        double *current_draw = m_fft_channel_left ? m_fftdrawl : m_fftdrawr;
        if (ImPlot::IsPlotHovered() && m_sound_data_x.size())
        {
            ImPlotPoint mouse_pos = ImPlot::GetPlotMousePos(IMPLOT_AUTO, ImAxis_Y1);
            char buffer[64];
            int mouse_to_index = 0;
            for(int i = 0; i < m_sound_data_x.size(); i++)
            {
                if (mouse_pos.x < m_fftfreqs[i]) break;
                mouse_to_index = i;
            }
            ImVec2 plotpos = ImPlot::GetPlotPos();
            ImPlotPoint pos = ImPlot::PixelsToPlot(ImVec2(plotpos.x + 110,plotpos.y + 20));
            snprintf(buffer, 64,  "Freq:%.2fKHz %0.2fdB", mouse_pos.x, current_draw[mouse_to_index]);
            ImPlot::PlotText(buffer, pos.x, pos.y);
        } 

        ImPlot::EndPlot();
    }
}

void AudioToolWindow::draw_channels_phase_widget(int plotheight)
{
    static float phase_limit_mult = 1.f;
    static float amplitude_limit_mult = 1.f;
    ImGui::SameLine();
    if (ImPlot::BeginPlot("L/R Phase & Amplitude diff", ImVec2(plotheight * 1.5, -1)))
    {
        ImPlot::SetupAxis(ImAxis_Y1, "Phase (degrees)");
        ImPlot::SetupAxis(ImAxis_Y2, "dB", ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxis(ImAxis_X1, "Time");

        ImPlot::SetupAxesLimits(ImAxis_Y1, m_phase_time.size(), -180.0 * phase_limit_mult, 180.0 * phase_limit_mult, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y2, -20. * amplitude_limit_mult, 20. * amplitude_limit_mult, ImPlotCond_Always);

        if (ImPlot::IsAxisHovered(ImAxis_Y1)){
            // Zoom Y axis in/out
            phase_limit_mult += ImGui::GetIO().MouseWheel * .01;
            if (phase_limit_mult < .1) phase_limit_mult = .1;
            if (phase_limit_mult > 1)  phase_limit_mult = 1;
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

void AudioToolWindow::draw_tone_generator_widget()
{
    
    const char* generator_presets[] = {"Sine","White noise", "Brown noise", "Pink noise"};
    static int fm_freq = 0;
    static float fm_vol = 1;
    static bool fm_enable = false;

    if (!m_sweep_started || m_async_sweep)
    {
        ImGui::BeginChild("ScopesChildToneGen", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);
        TextCenter("Signal generator");

        ImGui::BeginChild("ScopesChildTonGenSwitch", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::SetNextItemWidth(170);
        if (ImGui::Combo("Generator type", &m_signal_generator.mode(), generator_presets, 4))
        {
            m_signal_generator.set_mode(m_signal_generator.mode());
        }
        ImGui::SameLine();
        if(ImGui::ToggleButton("Tone generator", &m_signal_generator_switch))
        {
            reset_signal_generator();
        }
        ImGui::SetItemTooltip("Waveform generator ON/OFF");

        if (m_signal_generator.mode() == PAaudioWaveformGenerator::SINE)
        {
            ImGui::SameLine();
            if (ImGui::SliderInt("Pitch", &m_signal_generator_pitch, 20, 20000) || manage_slider_mousewheel_int(m_signal_generator_pitch, 20, 20000))
            {
                m_signal_generator.set_pitch(m_signal_generator_pitch);
            }
            ImGui::SetItemTooltip("Set the pitch of the sine generator");
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildToneVol", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (ImGui::SliderInt("Intensity", &m_signalgen_volume_db, -100, 0, "%d dB") || manage_slider_mousewheel_int(m_signalgen_volume_db, -100, 0))
        {
            m_signal_generator.set_volume(m_signalgen_volume_db);
        }
        ImGui::SetItemTooltip("Set the generator intensity");
        ImGui::EndChild();

        ImGui::EndChild();
    }
}

void AudioToolWindow::draw_rt_analysis_tab()
{
    int channelcount = m_audiorecorder.get_channel_count(); 
    float current_sample_rate = m_audiorecorder.get_current_samplerate();
    bool must_reinit_recorder = false;

    float frameh = ImGui::GetFrameHeightWithSpacing();
    float padh = 3.0f * ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ItemSpacing.y;

    draw_tone_generator_widget();

    ImGui::BeginChild("ScopesChildMain", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);
    float plotheight = height() / 2.0f - 5.f;

    ImGui::BeginChild("ScopesChildBar", ImVec2(0, plotheight), ImGuiChildFlags_Border, ImGuiWindowFlags_None);

    ImGui::BeginChild("ScopesChildCaptureOnOff", ImVec2(0, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
    if (ImGui::ToggleButton("Start", &m_compute_on))
    {
        m_audiorecorder.pause(!m_compute_on);
        if (m_audio_loopback_on) m_audioloopback.pause(!m_compute_on);
    }
    ImGui::SameLine();
    ImGui::ToggleButton("Trigger", &m_trigger_on);
    ImGui::SetItemTooltip("Start realtime capture");
    ImGui::SameLine();
    if (ImGui::ToggleButton("Audio loopback", &m_audio_loopback_on))
    {
        if (m_compute_on) m_audioloopback.pause(!m_audio_loopback_on);
    }
    ImGui::SetItemTooltip("Audio loopback (send recorder data to output device)");
    ImGui::EndChild();
    ImGui::SameLine();

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
    ImGui::BeginChild("ScopesChildTargetVolt", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
    const char* items[] = {"left", "right"};
    ImGui::ToggleButton("dB target", &m_use_targetdb);
    ImGui::SetItemTooltip("Set a target dB value relative to current measure");
    if (m_use_targetdb)
    {
        ImGui::SameLine();
        ImGui::ToggleButton("Lock", &m_lockdb);
        ImGui::SetItemTooltip("Lock the current measure");
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
    }
    ImGui::EndChild();

    /*
    * Calibration
    */

    ImGui::SameLine();
    ImGui::BeginChild("ScopesChildCalib", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
    static double rms_calibration = 1.0;
    if (m_rms_calibration_scale == 1.0)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Calibration");
        ImGui::SameLine();
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

    if (m_debug_info)
    {
        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildComputeInfo", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Audio process time: %luns", m_total_compute_time);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildComputeInfoUI", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("UI draw time : %luns", m_ui_time);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ScopesChildBufferInfoUI", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Record buffer : %.1f%%", m_audiorecorder.get_ringbuffer_occupation());
        ImGui::EndChild();
    }
    /*
    *   LCD voltmeter
    */
    if (m_show_rms_voltage)
    {
        draw_voltmeter_widget(channelcount);
    }

    draw_audio_time_domain_widget(plotheight, current_sample_rate, channelcount);
    
    ImGui::SameLine();
    if (m_show_wow_flutter)
    {
        draw_wow_flutter_widget(channelcount, current_sample_rate, plotheight);   
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
    if(ImGui::InputInt("Capture time (ms)", &m_recorder_latency_ms, 100, 200, ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if(m_recorder_latency_ms < 100) m_recorder_latency_ms = 100;
        if(m_recorder_latency_ms > 1000) m_recorder_latency_ms = 1000;
        must_reinit_recorder = true;
    }
    ImGui::SetItemTooltip("Audio sampling time in millisecond");
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("ShowThdChild", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
    if (ImGui::ToggleButton("Show THD", &m_show_thd)){}

    ImGui::SetItemTooltip("Enable HD overlay");
    ImGui::EndChild();

    ImGui::EndChild();

    draw_audio_fft_widget(channelcount, current_sample_rate, plotheight);

    if (m_compute_channel_phase)
    {
        draw_channels_phase_widget(plotheight);
    }

    ImGui::EndChild();

    ImGui::EndChild();

    if (must_reinit_recorder)
    {
        if (m_measure_delay < m_recorder_latency_ms * 2)
        {
            m_measure_delay = m_recorder_latency_ms * 2;
            m_sweep_time    = m_measure_delay;
        }
        reinit_recorder();
    }
}

