#include "main_widget.h"

void AudioToolWindow::draw_sdr()
{       
    static bool start_sdr = false;
    static int  tuner_gain_id = 0;

    std::vector<int> tuner_gains = m_sdr_thread.get_scanner().get_rtl_device().get_tuner_gains();
    std::vector<std::string> combo_gains;
    for (auto tg : tuner_gains)
    {
        std::string curr = std::to_string(float(tg) / 10.);
        curr.resize(5);
        combo_gains.push_back(curr);
    }

    ImGui::BeginChild("ScopesChildSDROnOff", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);

    ImGui::BeginChild("ScopesChild2", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
    if (ImGui::ToggleButton("ON/OFF", &start_sdr))
    {
        if (start_sdr)
        {
            m_sdr_thread.start();
        }
        else
        {
            m_sdr_thread.stop();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("ScopesChildSDRSettings", ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
    ImGui::SetNextItemWidth(80);
    if (ImGui::Combo("Tuner gain", &tuner_gain_id, vector_getter, (void *)&combo_gains, combo_gains.size()))
    {
        m_sdr_thread.get_scanner().get_settings().gain = tuner_gains[tuner_gain_id];
        m_sdr_thread.get_scanner().apply();
    }
    ImGui::EndChild();

    if (ImPlot::BeginPlot("SDR FFT", ImVec2(-1, -1)))
    {
        ImPlot::SetupAxes("Frequency (MHz)", "dBm", 0, ImPlotAxisFlags_Lock);
        // if (m_logscale_frequency){
        //     ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        // }

        float freq_start = m_sdr_thread.get_scanner_settings().lower_freq / 1000000;
        float freq_stop = m_sdr_thread.get_scanner_settings().upper_freq / 1000000;

        ImPlot::SetupAxesLimits(freq_start, freq_stop, -65.0, 40.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, freq_start, freq_stop);

        m_sdr_thread.lock_graph();
        const std::vector<SDR_Scanner::Scan_result> scan_res = m_sdr_thread.get_scan_result();
        if (scan_res.size())
        {
            for (int i = 0; i < scan_res.size(); ++i)
            {
                ImPlot::PlotLine("RF FFT", scan_res[i].buffer_x.data(), scan_res[i].buffer.data(), scan_res[i].buffer_x.size() - 1);
            }
        }
        m_sdr_thread.unlock_graph();

        ImPlot::EndPlot();
    }

    ImGui::EndChild();
}
