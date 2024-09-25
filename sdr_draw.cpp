#include "main_widget.h"

void AudioToolWindow::draw_sdr()
{       

    ImGui::BeginChild("ScopesChild1", ImVec2(0, height()), ImGuiChildFlags_Border, ImGuiWindowFlags_None);

    ImGui::BeginChild("ScopesChild2", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
    if (ImGui::Button("Start"))
    {
        
    }

    ImGui::EndChild();

    if (ImPlot::BeginPlot("SDR FFT", ImVec2(-1, -1)))
    {
        ImPlot::SetupAxes("Frequency (MHz)", "dBm", 0, ImPlotAxisFlags_Lock);
        // if (m_logscale_frequency){
        //     ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        // }
        ImPlot::SetupAxesLimits(420, 440, -60.0, 40.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 420, 440);

        m_sdr_thread.lock_graph();
        const std::vector<SDR_Scanner::Scan_result> scan_res = m_sdr_thread.get_scan_result();
        if (scan_res.size())
        {
            for (int i = 0; i < scan_res.size(); ++i)
            {
                ImPlot::PlotLine("RF FFT", scan_res[i].buffer_x.data(), scan_res[i].buffer.data(), scan_res[i].buffer_x.size());
            }
        }
        m_sdr_thread.unlock_graph();

        ImPlot::EndPlot();
    }

    ImGui::EndChild();
}
