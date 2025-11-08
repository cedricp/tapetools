#include "main_widget.h"

AudioToolWindow::AudioToolWindow(Window_SDL* win) : Widget(win, "AudioTools"), m_audiorecorder(m_audiomanager), m_audioplayer(m_audiomanager)
{
    set_maximized(true);
    set_movable(false);
    set_resizable(false);
    set_titlebar(false);

    compute_fft_window_corrections();
    reset_audiomanager();
    set_theme();

    //m_audiomanager.device_changed_event.connect_event(STATIC_METHOD(on_device_changed), this);
    //m_audiomanager.backend_disconnected_event.connect_event(STATIC_METHOD(on_backend_disconnected), this);

    m_audiomanager.flush();

    ImPlotStyle& s = ImPlot::GetStyle();
    s.LineWeight = 1.5f;
    s.PlotBorderSize = 2.f;
}

AudioToolWindow::~AudioToolWindow()
{
    m_signal_generator.destroy();
#ifdef RTL_SDR
    m_sdr_thread.stop();
    m_sdr_thread.join();
#endif
    destroy_capture();
}

void AudioToolWindow::set_theme()
{
    if (m_uitheme == 0)
        ImGui::StyleColorsDark();
    else if (m_uitheme == 1)
        ImGui::StyleColorsLight();
    else if (m_uitheme == 2)
        ImGui::StyleColorsClassic();
    else if (m_uitheme == 3)
    {
        auto &colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg] = ImVec4{0.1f, 0.1f, 0.13f, 1.0f};
        colors[ImGuiCol_MenuBarBg] = ImVec4{0.16f, 0.16f, 0.21f, 1.0f};

        // Border
        colors[ImGuiCol_Border] = ImVec4{0.44f, 0.37f, 0.61f, 0.29f};
        colors[ImGuiCol_BorderShadow] = ImVec4{0.0f, 0.0f, 0.0f, 0.24f};

        // Text
        colors[ImGuiCol_Text] = ImVec4{1.0f, 1.0f, 1.0f, 1.0f};
        colors[ImGuiCol_TextDisabled] = ImVec4{0.5f, 0.5f, 0.5f, 1.0f};

        // Headers
        colors[ImGuiCol_Header] = ImVec4{0.13f, 0.13f, 0.17, 1.0f};
        colors[ImGuiCol_HeaderHovered] = ImVec4{0.19f, 0.2f, 0.25f, 1.0f};
        colors[ImGuiCol_HeaderActive] = ImVec4{0.16f, 0.16f, 0.21f, 1.0f};

        // Buttons
        colors[ImGuiCol_Button] = ImVec4{0.23f, 0.23f, 0.27, 1.0f};
        colors[ImGuiCol_ButtonHovered] = ImVec4{0.29f, 0.3f, 0.35f, 1.0f};
        colors[ImGuiCol_ButtonActive] = ImVec4{0.36f, 0.36f, 0.41f, 1.0f};
        colors[ImGuiCol_CheckMark] = ImVec4{0.74f, 0.58f, 0.98f, 1.0f};

        // Popups
        colors[ImGuiCol_PopupBg] = ImVec4{0.1f, 0.1f, 0.13f, 0.92f};

        // Slider
        colors[ImGuiCol_SliderGrab] = ImVec4{0.44f, 0.37f, 0.61f, 0.54f};
        colors[ImGuiCol_SliderGrabActive] = ImVec4{0.74f, 0.58f, 0.98f, 0.54f};

        // Frame BG
        colors[ImGuiCol_FrameBg] = ImVec4{0.13f, 0.13, 0.17, 1.0f};
        colors[ImGuiCol_FrameBgHovered] = ImVec4{0.19f, 0.2f, 0.25f, 1.0f};
        colors[ImGuiCol_FrameBgActive] = ImVec4{0.16f, 0.16f, 0.21f, 1.0f};

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4{0.16f, 0.16f, 0.21f, 1.0f};
        colors[ImGuiCol_TabHovered] = ImVec4{0.24, 0.24f, 0.32f, 1.0f};
        colors[ImGuiCol_TabActive] = ImVec4{0.2f, 0.22f, 0.27f, 1.0f};
        colors[ImGuiCol_TabUnfocused] = ImVec4{0.16f, 0.16f, 0.21f, 1.0f};
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.16f, 0.16f, 0.21f, 1.0f};

        // Title
        colors[ImGuiCol_TitleBg] = ImVec4{0.16f, 0.16f, 0.21f, 1.0f};
        colors[ImGuiCol_TitleBgActive] = ImVec4{0.16f, 0.16f, 0.21f, 1.0f};
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{0.16f, 0.16f, 0.21f, 1.0f};

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg] = ImVec4{0.1f, 0.1f, 0.13f, 1.0f};
        colors[ImGuiCol_ScrollbarGrab] = ImVec4{0.16f, 0.16f, 0.21f, 1.0f};
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4{0.19f, 0.2f, 0.25f, 1.0f};
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4{0.24f, 0.24f, 0.32f, 1.0f};

        // Seperator
        colors[ImGuiCol_Separator] = ImVec4{0.44f, 0.37f, 0.61f, 1.0f};
        colors[ImGuiCol_SeparatorHovered] = ImVec4{0.74f, 0.58f, 0.98f, 1.0f};
        colors[ImGuiCol_SeparatorActive] = ImVec4{0.84f, 0.58f, 1.0f, 1.0f};

        // Resize Grip
        colors[ImGuiCol_ResizeGrip] = ImVec4{0.44f, 0.37f, 0.61f, 0.29f};
        colors[ImGuiCol_ResizeGripHovered] = ImVec4{0.74f, 0.58f, 0.98f, 0.29f};
        colors[ImGuiCol_ResizeGripActive] = ImVec4{0.84f, 0.58f, 1.0f, 0.29f};
    }
    else if (m_uitheme == 4)
    {
        auto &colors = ImGui::GetStyle().Colors;

        colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 0.90f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.05f, 0.10f, 0.85f);
        colors[ImGuiCol_Border] = ImVec4(0.70f, 0.70f, 0.70f, 0.65f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.00f, 0.00f, 0.01f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.80f, 0.80f, 0.40f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.65f, 0.65f, 0.45f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.83f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.40f, 0.40f, 0.80f, 0.20f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.00f, 0.00f, 0.00f, 0.87f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.01f, 0.01f, 0.02f, 0.80f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.20f, 0.25f, 0.30f, 0.60f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.55f, 0.53f, 0.55f, 0.51f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.56f, 0.56f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.91f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.90f, 0.90f, 0.90f, 0.83f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.70f, 0.70f, 0.70f, 0.62f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.30f, 0.30f, 0.84f);
        colors[ImGuiCol_Button] = ImVec4(0.48f, 0.72f, 0.89f, 0.49f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.50f, 0.69f, 0.99f, 0.68f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.30f, 0.69f, 1.00f, 0.53f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.44f, 0.61f, 0.86f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.38f, 0.62f, 0.83f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.70f, 0.60f, 0.60f, 1.00f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.90f, 0.70f, 0.70f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.85f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
        colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 0.00f, 1.00f, 0.35f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
    }

    ImGui::GetStyle().FrameRounding = 4.0;
    ImGui::GetStyle().ChildRounding = 4.0;
    ImGui::GetStyle().WindowRounding = 4.0;
    ImGui::GetStyle().GrabRounding = 4.0;
    ImGui::GetStyle().GrabMinSize = 4.0;
    ImGui::GetStyle().WindowPadding = ImVec2(4.0, 4.0);
}

void AudioToolWindow::check_settings_loaded()
{
    static bool settings_loaded = false;
    if (!settings_loaded)
    {
        if (GImGui->SettingsLoaded)
            printf("Settings restored\n");
        settings_loaded = true;
        reset_audiomanager();
    }
}

void AudioToolWindow::draw()
{
    m_audiomanager.flush();
    check_settings_loaded();
    Chrono chrono;

    if (ImGui::BeginMainMenuBar())
    {
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
                if (ImGui::MenuItem("Dracula", nullptr, nullptr))
                {
                    m_uitheme = 3;
                    set_theme();
                }
                if (ImGui::MenuItem("Grey", nullptr, nullptr))
                {
                    m_uitheme = 4;
                    set_theme();
                }
                ImGui::EndMenu();
            }
            ImGui::MenuItem("Sound card setup", nullptr, &m_sound_setup_open);
            ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            if (ImGui::MenuItem("Optimized FFT compute", nullptr, &m_optimized_fft))
            {
                reinit_recorder();
            }
            ImGui::MenuItem("Show debug info", nullptr, &m_debug_info);
            ImGui::PopItemFlag();

            ImGui::Separator();
            if (ImGui::MenuItem("Make a donation", nullptr, nullptr))
            {
#ifdef WIN32
                ShellExecute(NULL, TEXT("open"),
                             TEXT("https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=cedricpaille@gmail.com&lc=CY&item_name=codetronic&currency_code=EUR&bn=PP%2dDonationsBF%3abtn_donateCC_LG.if:NonHosted"),
                             NULL, NULL, SW_SHOWNORMAL);
#else

#endif
            }

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
#ifdef RTL_SDR
    if (m_sdr_thread.get_scanner().get_rtl_device().get_device_count() && ImGui::BeginTabItem("SDR analysis"))
    {
        draw_sdr();
        ImGui::EndTabItem();
    }
#endif
    ImGui::EndTabBar();

    draw_tools_windows();

    draw_log_window();

    m_ui_time = chrono.get_elapsed_time();
}

void AudioToolWindow::start_sweep_gen()
{
    reinit_recorder();

    m_audiorecorder.pause(false);
    m_compute_on = true;
    m_sweep_started = true;
    m_sweep_target_frequency = 20;
    m_sweep_freqs.clear();
    m_sweep_values.clear();
    m_recorder_latency_ms = 100;
    reinit_recorder();

    if (!m_async_sweep)
    {
        m_signal_generator_switch = true;
        reset_signal_generator();
        m_signal_generator.set_mode(PAaudioWaveformGenerator::SINE);
        m_signal_generator.set_pitch(m_sweep_target_frequency);
    }

    m_sweep_status = true;
    m_sweep_timer_chrono.reset();
    m_sweep_last_measure_freq = 10;
}

void AudioToolWindow::stop_sweep_gen()
{
    m_signal_generator_switch = false;
    m_signal_generator.pause();
    m_sweep_status = false;
    m_sweep_started = false;
}

void AudioToolWindow::draw_tools_windows()
{
    if (m_sound_setup_open && !m_sweep_started)
    {
        ImGui::SetNextWindowSize(ImVec2(600, 200));
        if (ImGui::Begin("Sound card setup", &m_sound_setup_open))
        {
            ImVec2 winsize = ImGui::GetWindowSize();
            ImGui::PushItemWidth(winsize.x / 3);
            const std::vector<std::string> &out_devices = m_audiomanager.get_output_devices();
            ImGui::SeparatorText("Output device");
            if (ImGui::Combo("Ouput", &m_combo_out, vector_getter, (void *)&out_devices, out_devices.size()))
            {
                m_audio_out_idx = m_audiomanager.get_output_device_map(m_combo_out);
                m_output_device = out_devices[m_combo_out];
                reset_signal_generator();
            }
            ImGui::SameLine();
            const std::vector<std::string> out_samplerate = m_audio_out_idx >= 0 ? m_audiomanager.get_output_sample_rates_str(m_audio_out_idx) : std::vector<std::string>();
            if (ImGui::Combo("Samplerate##1", &m_out_sample_rate_idx, vector_getter, (void *)&out_samplerate, out_samplerate.size()))
            {
                reset_signal_generator();
            }

            ImGui::SeparatorText("Input device");
            const std::vector<std::string> &in_devices = m_audiomanager.get_input_devices();
            if (ImGui::Combo("Input", &m_combo_in, vector_getter, (void *)&in_devices, in_devices.size()))
            {
                m_audio_in_idx = m_audiomanager.get_input_device_map(m_combo_in);
                m_input_device = in_devices[m_combo_in];
                reinit_recorder();
            }
            ImGui::SameLine();
            const std::vector<std::string> in_samplerate = m_audio_in_idx >= 0 ? m_audiomanager.get_input_sample_rates_str(m_audio_in_idx) : std::vector<std::string>();
            if (ImGui::Combo("Samplerate##2", &m_in_sample_rate_idx, vector_getter, (void *)&in_samplerate, in_samplerate.size()))
            {
                reinit_recorder();
            }
            ImGui::SeparatorText("Output loopback device");
            if (ImGui::Combo("Output", &m_combo_out_loopback, vector_getter, (void *)&out_devices, out_devices.size()))
            {
                m_audio_loopback_out_idx = m_audiomanager.get_output_device_map(m_combo_out_loopback);
                m_output_loopback_device = out_devices[m_combo_out];
                reinit_recorder();
            }
            ImGui::PopItemWidth();
        }
        ImGui::End();
    }
}

void AudioToolWindow::save_soundcard_setup(std::map<std::string, std::string> &cnf)
{
    const std::vector<std::string> &out_devices = m_audiomanager.get_output_devices();
    std::string out_device = out_devices[m_combo_out];
    const std::vector<std::string> &in_devices = m_audiomanager.get_input_devices();
    std::string in_device = in_devices[m_combo_in];
    const std::vector<std::string> &out_reroute_devices = m_audiomanager.get_output_devices();
    std::string out_reroute_device = out_devices[m_combo_out_loopback];

    cnf["output_device"] = out_device;
    cnf["input_device"] = in_device;
    cnf["output_loopback_device"] = out_reroute_device;
}

void AudioToolWindow::get_configuration_string(std::map<std::string, std::string> &cnf)
{
    save_soundcard_setup(cnf);
}

void AudioToolWindow::set_configuration_string(std::string s, std::string str)
{
    if (s == "input_device")
    {
        const std::vector<std::string> &in_devices = m_audiomanager.get_input_devices();
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
        const std::vector<std::string> &out_devices = m_audiomanager.get_output_devices();
        std::vector<std::string>::const_iterator it = std::find(out_devices.begin(), out_devices.end(), str);
        if (it != out_devices.end())
        {
            m_combo_out = std::distance(out_devices.begin(), it);
            m_audio_out_idx = m_audiomanager.get_output_device_map(m_combo_out);
            printf("Restoring saved output dev found [%s] [%i]\n", str.c_str(), m_audio_out_idx);
            m_output_device = str;
        }
    }

    if (s == "output_loopback_device")
    {
        const std::vector<std::string> &out_devices = m_audiomanager.get_output_devices();
        std::vector<std::string>::const_iterator it = std::find(out_devices.begin(), out_devices.end(), str);
        if (it != out_devices.end())
        {
            m_combo_out_loopback = std::distance(out_devices.begin(), it);
            m_audio_loopback_out_idx = m_audiomanager.get_output_device_map(m_combo_out_loopback);
            printf("Restoring saved output loopback dev found [%s] [%i]\n", str.c_str(), m_audio_loopback_out_idx);
            m_output_device = str;
        }
    }
}

void AudioToolWindow::get_configuration_int(std::map<std::string, int> &cnf)
{
    cnf["logScaleFFT"] = m_logscale_frequency == true ? 1 : 0;
    cnf["FFTwindowType"] = m_fft_window_fn_index;
    cnf["showVoltmeter"] = m_show_rms_voltage == true ? 1 : 0;
    cnf["theme"] = m_uitheme;
    cnf["optimizedFFT"] = m_optimized_fft == true ? 1 : 0;
    cnf["inSampleRateIdx"] = m_in_sample_rate_idx;
    cnf["outSampleRateIdx"] = m_out_sample_rate_idx;
}

void AudioToolWindow::log_message(std::string msg)
{
    m_debug_logs.push_back(msg);
    if (m_debug_logs.size() > 20)
        m_debug_logs.erase(m_debug_logs.begin());
    printf("%s\n", msg.c_str());
    m_show_log_window = true;
}

void AudioToolWindow::draw_log_window()
{
    if (m_show_log_window)
    {
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug log", &m_show_log_window))
        {
            ImGui::BeginChild("LogRegion");
            for (auto &log : m_debug_logs)
            {
                ImGui::TextUnformatted(log.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
        ImGui::End();
    }
}

void AudioToolWindow::set_configuration_int(std::string s, int i)
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

void AudioToolWindow::get_configuration_float(std::map<std::string, float> &cnf)
{
    cnf["calibrationValue"] = m_rms_calibration_scale;
}

void AudioToolWindow::set_configuration_float(std::string s, float f)
{
    if (s == "calibrationValue")
        m_rms_calibration_scale = f;
}

bool AudioToolWindow::check_data_buffer()
{
    const int min_wanted_buffer_size = m_capture_size * m_audiorecorder.get_channel_count();
    if (m_audiorecorder.get_available_samples() >= min_wanted_buffer_size)
    {
        bool computed = compute();
        if (computed)
        {
            if (m_sweep_status && m_sweep_timer_chrono.get_elapsed_time() > m_measure_delay * 1000){
                process_sweep();
            }
            if (m_compute_channel_phase || m_show_thd)
                compute_thd();
            if (m_show_thd)
                compute_thdn();
            if (m_compute_channel_phase)
                compute_channels_phase();
        }

        if (computed)
            update_ui();
        return true;
    }
#ifdef RTL_SDR
    if (m_sdr_thread.data_available())
    {
        update_ui();
        return true;
    }
#endif

    return false;
}

void AudioToolWindow::set_sound_config()
{
    if (m_input_device.empty()) m_audio_in_idx  = m_audiomanager.get_default_input_device_id();
    if (m_output_device.empty()) m_audio_out_idx = m_audiomanager.get_default_output_device_id();

    m_combo_in  = m_audiomanager.get_input_device_reverse_map(m_audio_in_idx);
    m_combo_out = m_audiomanager.get_output_device_reverse_map(m_audio_out_idx);
}

void AudioToolWindow::reset_audiomanager()
{
    set_sound_config();
    reinit_recorder();
    reset_signal_generator();
}

// Callbacks methods

IMPLEMENT_CALLBACK_METHOD(on_device_changed, AudioToolWindow)
{
    // reinit_recorder();
    log_message("Audio device configuration changed.\n");
}

IMPLEMENT_CALLBACK_METHOD(on_backend_disconnected, AudioToolWindow)
{
    reset_audiomanager();
}
