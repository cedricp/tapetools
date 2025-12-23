#define _USE_MATH_DEFINES
#include "main_widget.h"
#include "wow_flutter_thread.h"
#include <complex>
#include <algorithm>

void wait_wow_thread()
{
    while(App_SDL::get()->get_thread("WFtask")){
        App_SDL::get()->release_finished_threads();
        // Check is previous thread has terminated, if not, reject these samples to not overload
        usleep(100000);
    }
}

void AudioToolWindow::compute_fft_window_cache()
{
    if (m_current_window_cache != nullptr) delete[] m_current_window_cache;
    m_current_window_cache = new double[m_capture_size];

    for(int i = 0; i < m_capture_size; ++i) m_current_window_cache[i] = m_window_fn(i, m_capture_size);
}

void AudioToolWindow::set_window_fn(bool compute_cache)
{
    switch (m_fft_window_fn_index)
    {
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
            m_window_fn = kaiser6_fft_window;
            break;
        default:
            m_window_fn = rectangle_fft_window;
            break;
    }

    if (compute_cache) compute_fft_window_cache();
}

void AudioToolWindow::detect_periods(){
    const double timestep = 1. / (double)m_audiorecorder.get_current_samplerate();
    double const* audio_data = m_sound_data1.data();
    std::vector<double> frequencies;

    int previous_idx = 0;
    double lastzerocross = 0;
    double previous = audio_data[0];
    double previous_time = 0;
    double freq_mean = 0;
    m_trigger_index = -1;

    for (int i = 1; i < m_capture_size; ++i)
    {
        double current = audio_data[i];
        
        if (previous < 0 && current > 0)
        {
            double a[2] = {timestep * ((double)i-1.), previous};
            double b[2] = {timestep * (double)i, current};
            double zcrosstime = zerocross(a,b);
            if (lastzerocross > 0)
            {
                double freq = 1.0 / (zcrosstime - lastzerocross);
                frequencies.push_back(freq);
                freq_mean += freq;
            }
            lastzerocross = zcrosstime;
            if (m_trigger_index < 0){
                m_trigger_index = i;
            }
        }
        previous = current;
    }

    if (frequencies.size() < 2)
    {
        m_frequency_counter = 0;
        return;
    }

    freq_mean /= (double)frequencies.size();
    m_frequency_counter = freq_mean;
    double last = frequencies[0];
    double maxdeviation = 0;
    for (int i = 1; i < frequencies.size();++i){
        double diff = fabs(last - frequencies[i]);
        if (diff > maxdeviation) maxdeviation = diff;
    }
}

void AudioToolWindow::compute_fft_window_corrections(int num_samples)
{
    int tmp = m_fft_window_fn_index;
    const double inv_num_samples = 1. / num_samples;
    for (int j = 0; j < 8; ++j)
    {
        m_fft_window_fn_index = j;
        set_window_fn(false);
        double sum = 0;
        double rms = 0;
        for (int i = 0; i < num_samples; i++)
        {
            double val = m_window_fn(i, num_samples);
            sum += val;
            rms += val*val;
        }

        // Normalization
        m_window_amplitude_correction[j] = 1.0 / (sum * inv_num_samples);
        m_window_energy_correction[j] = 1.0 / sqrt(rms * inv_num_samples);
    }
    // Restore
    m_fft_window_fn_index = tmp;
}

bool AudioToolWindow::compute()
{
    const int channelcount = m_audiorecorder.get_channel_count();
    if (channelcount == 0)
    {
        return false;
    }

    bool data_available = m_audiorecorder.get_data(m_raw_buffer, m_capture_size * channelcount);
    if (!data_available){
        // Quick return in no new audio data to compute
        return false;
    }

    if (!m_compute_on){
        // Just consume data, if any
        return false;
    }

    // Fill audio for audio loopback
    if (m_audio_loopback_on)
    {
        m_audioloopback.add_data(m_raw_buffer.data(), m_capture_size * channelcount);
    }
    
    const int fft_capture_size = m_capture_size / 2;
    const double current_sample_rate = m_audiorecorder.get_current_samplerate();
    const double half_sample_rate = current_sample_rate / 2.0;
    const double inv_current_sample_rate = 1.0 / current_sample_rate;
    const double inv_fft_capture_size = 1.0 / float(fft_capture_size);
    const double fft_step = half_sample_rate * inv_fft_capture_size;
    m_fft_highest_val = -100;
    
    if (m_sound_data1.size() != m_capture_size) m_sound_data1.resize(m_capture_size);
    if (m_sound_data2.size() != m_capture_size) m_sound_data2.resize(m_capture_size);
    if (m_sound_data_x.size() != m_capture_size) m_sound_data_x.resize(m_capture_size);

    m_rms_left = m_rms_right = 0.0;

    Chrono chrono;
    for (int i = 0; i < m_capture_size; i++)
    {
        double sound_data = m_raw_buffer[i*channelcount] * m_audio_gain;
        m_sound_data1[i] = sound_data;
        
        // Apply window coeffs
        m_fftinl[i] = sound_data * m_current_window_cache[i];
        m_sound_data_x[i] = float(i) * inv_current_sample_rate * 1000.0/* ->ms */;

        m_rms_left += m_sound_data1[i] * m_sound_data1[i];
        if(channelcount>1)
        {
            m_sound_data2[i] = m_raw_buffer[i*channelcount+1] * m_audio_gain;
            m_fftinr[i] = m_sound_data2[i] * m_current_window_cache[i];
            m_rms_right += m_sound_data2[i] * m_sound_data2[i];
        }
    }

    if (m_show_wow_flutter){
        // Audio data ready, launch W&F measurement as soon as possible in parallel
        compute_wow_and_flutter();
    }

    detect_periods();

    m_rms_left = m_rms_left / m_capture_size;
    m_rms_left = sqrt(m_rms_left);

    if (channelcount > 1)
    {
        m_rms_right = m_rms_right / m_capture_size;
        m_rms_right = sqrt(m_rms_right);
    }
    
    double* current_fft_draw = m_fft_channel_left  ? m_fftdrawl : m_fftdrawr;

    // Compute and fill audio FFT
    ::fftw_execute(m_fftplanl);
    if (channelcount > 1) ::fftw_execute(m_fftplanr);

    std::vector<double> fftdatal(fft_capture_size);
    float sum = 0;
    for (int i = 0; i < fft_capture_size; ++i)
    {
        m_fftfreqs[i] = fft_step * (double)(i);
        double fftout = complex_module(m_fftoutl[i][0], m_fftoutl[i][1]) * inv_fft_capture_size;
        fftout *= m_window_amplitude_correction[m_fft_window_fn_index];
        fftout = std::max(linear_to_db(fftout), -200.0);
        m_fftdrawl[i] = std::isnan(fftout) ? -200.f : fftout;
        if (m_fft_channel_left) sum += fftout;

        if (channelcount > 1)
        {
            double fftout = complex_module(m_fftoutr[i][0], m_fftoutr[i][1]) * inv_fft_capture_size;
            fftout *= m_window_amplitude_correction[m_fft_window_fn_index];
            fftout = std::max(linear_to_db(fftout), -200.0);
            m_fftdrawr[i] = std::isnan(fftout) ? -200.f : fftout;
            if (m_fft_channel_right) sum += fftout;
        }
    }

    m_fftdrawr[0] *= 0.5;
    m_fftdrawl[0] *= 0.5;

    double mean = sum * inv_fft_capture_size;
    double stddev = 0;
    for (int i = 0; i < fft_capture_size; ++i)
    {
        double a = (current_fft_draw[i] - mean);
        stddev += a * a;
    }
    stddev = sqrt(stddev / float(fft_capture_size - 1));
    m_noise_foor = mean + stddev;

    m_total_compute_time = chrono.get_elapsed_time();

    return true;
}


void AudioToolWindow::compute_wow_and_flutter()
{
    double samplerate = m_audiorecorder.get_current_samplerate();

    // Append captured audio data to get them
    const int audio_capture_length = WOW_FLUTTER_ANALYSIS_TIME * samplerate;
    const std::vector<double> &audio_channel = m_fft_channel_left ? m_sound_data1 : m_sound_data2;

    const int sampled_audio_length = audio_channel.size();

    m_wow_data_mutex.lock();
    if (m_longterm_audio.empty())
    {
        m_longterm_audio.reserve(audio_capture_length);
    }

    if (m_longterm_audio.size() < audio_capture_length)
    {
        // Just add audio data to buffer
        m_longterm_audio.insert(m_longterm_audio.end(), audio_channel.begin(), audio_channel.end());
    }
    else
    {
        // move the buffer backward and replace the end with new data
        int move_size = audio_capture_length - sampled_audio_length;
        memcpy(&m_longterm_audio[0], &m_longterm_audio[sampled_audio_length], move_size*sizeof(double));
        memcpy(&m_longterm_audio[move_size], &audio_channel[0], sampled_audio_length*sizeof(double));
    }
    m_wow_data_mutex.unlock();

    if (m_longterm_audio.size() < audio_capture_length)
    {
        // Wait buffer to be completly filled
        return;
    }
    
    App_SDL::get()->release_finished_threads();
    if(App_SDL::get()->get_thread("WFtask")){
        // Check is previous thread has terminated, if not, reject these samples to not overload the UI
        log_message("Wow and flutter thread too slow... Some audio data will be dropped.");
        return;
    }

    int reference_frequency = 3000;
    if (m_wow_test_frequency == 1) reference_frequency = 3150;
    else if (m_wow_test_frequency == 2) reference_frequency = m_wow_test_frequency_custom;

    // Create and launch thread
    WowAndFluterThread* wt = new WowAndFluterThread(*this, reference_frequency, samplerate);

    wt->start(true);
}

void AudioToolWindow::compute_channels_phase()
{
    if (m_audiorecorder.get_channel_count() < 2) return;

    float fft_capture_size = m_capture_size / 2;

    // Get the fundamental frequency FFT result
    fftw_complex* right_comp = &(m_fftoutl[m_fft_harmonics_idx[0]]);
    fftw_complex* left_comp = &(m_fftoutr[m_fft_harmonics_idx[0]]);

    // Compute the phase (complex argument) of left and right channels
    double right_phase = wrap_phase(atan2((*right_comp)[1], (*right_comp)[0]));
    double left_phase = wrap_phase(atan2((*left_comp)[1], (*left_comp)[0]));
    // Compute the phase difference and convert to degrees
    m_phase_diff_degrees = wrap_phase(right_phase - left_phase) * 180. / M_PI;

    // Compute amplitude difference (diff of complex modules)
    double left_amplitude  = complex_module((*left_comp)[0], (*left_comp)[1]) / fft_capture_size;
    double right_amplitude = complex_module((*right_comp)[0], (*right_comp)[1]) / fft_capture_size;

    // Convert to dB
    m_left_right_db = 20. * log10(left_amplitude / right_amplitude);

    if (m_phase_time.size() < 200)
    {
        m_phase_history.push_back(m_phase_diff_degrees);
        m_lrdiff_history.push_back(m_left_right_db);
        m_phase_time.push_back(m_phase_time.size());
    }
    else 
    {
        memcpy(m_phase_history.data(), &m_phase_history[1], (m_phase_history.size() - 1) * sizeof(float));
        memcpy(m_lrdiff_history.data(), &m_lrdiff_history[1], (m_lrdiff_history.size() - 1) * sizeof(float));
        m_phase_history.back() = m_phase_diff_degrees;
        m_lrdiff_history.back() = m_left_right_db;
    }
}

void AudioToolWindow::compute_thd()
{
    double const* current_fft_draw = m_fft_channel_left ? m_fftdrawl : m_fftdrawr;
    const int fft_capture_size = m_capture_size / 2;
    m_fft_found_peaks = 1;

    // Find max values of filtered signal
    int fundamental_index = 0;
    double max = -200.;
    for(int i = 0; i < fft_capture_size; ++i)
    {
        if (current_fft_draw[i] > max)
        {
            max = current_fft_draw[i];
            fundamental_index = i; 
        }
    }

    m_fft_harmonics_idx[0] = fundamental_index;
    m_fft_harmonics_freq[0] = m_fftfreqs[fundamental_index];

    for (int i = 1; i < 8; ++i)
    {
        int i_order_harmonic = fundamental_index * (i+1);
        if (i_order_harmonic > fft_capture_size) break;
        m_fft_found_peaks++;

        m_fft_harmonics_idx[i] = i_order_harmonic;
        m_fft_harmonics_freq[i] = m_fftfreqs[i_order_harmonic];
    }

    // Compute Total Harmonic Distortion
    // Source http://www.r-type.org/addtext/add183.htm
    // This method also works and give same results as the one below

    // if (m_fft_found_peaks)
    // {
    //     m_thd = 0;
    //     double fundamental_db = current_fft_draw[m_fft_harmonics_idx[0]];
    //     double totdbc = 0;
    //     for (int i = 1; i < m_fft_found_peaks; ++i)
    //     {
    //         double dBc = current_fft_draw[m_fft_harmonics_idx[i]] - fundamental_db;
    //         totdbc += pow(10.0, dBc / 10.0);
    //     }
    //     m_thd = sqrt(totdbc) * 100.;
    // }

    if (m_fft_found_peaks)
    {
        m_thd = 0;
        double fundamental_mod = complex_module(m_fftoutl[m_fft_harmonics_idx[0]][0], m_fftoutl[m_fft_harmonics_idx[0]][1]);
        fundamental_mod *= m_window_amplitude_correction[m_fft_window_fn_index];

        double total = 0;
        for (int i = 1; i < m_fft_found_peaks; ++i)
        {
            double mod = complex_module(m_fftoutl[m_fft_harmonics_idx[i]][0], m_fftoutl[m_fft_harmonics_idx[i]][1]);
            mod *= m_window_amplitude_correction[i];
            total += mod*mod / (fundamental_mod*fundamental_mod);
        }
        m_thd = sqrt(total) * 100.;
    }
}

void AudioToolWindow::compute_thdn()
{
    const int fft_capture_size = m_capture_size/2;
    fftw_complex const* current_fft = m_fft_channel_left ? m_fftoutl : m_fftoutr;
    const double invsqrt2 = 1.0 / sqrt(2.0);
    const double inv_capture_size = 1.0 / (double(fft_capture_size));

    m_thdn = m_thddb = 0.;

    double max_val = -200;
    int max_val_index = 0;

    m_fft_rms = 0;
    for (int i = 1; i < fft_capture_size; ++i)
    {
        double fft_module = complex_module(current_fft[i][0], current_fft[i][1]);
        fft_module *= m_window_energy_correction[m_fft_window_fn_index];
        fft_module *= fft_module;
        m_fft_rms  += fft_module;
        m_fft_modules[i] = fft_module;
        
        if (fft_module > max_val)
        {
            max_val = fft_module;
            max_val_index = i;
        }
    }
    m_fft_rms = sqrt(m_fft_rms) * invsqrt2 * inv_capture_size;

    // Find FFT fundamental range
    double tmp = max_val;
    for (int i = max_val_index; i < fft_capture_size; ++i)
    {
        if (m_fft_modules[i] > tmp)
        {
            m_fft_fund_idx_range_max = i;
            break;
        }
        tmp = m_fft_modules[i];
    }
    
    tmp = max_val;
    for (int i= max_val_index; i >= 0; --i)
    {
        if (m_fft_modules[i] > tmp)
        {
            m_fft_fund_idx_range_min = i;
            break;
        }
        tmp = m_fft_modules[i];
    }

    if (m_fft_fund_idx_range_max - m_fft_fund_idx_range_min <=0)
    {
        m_fft_fund_idx_range_max = m_fft_fund_idx_range_min = 0;
        return;
    }

    double noise_rms = 0;
    // Start at 1, we don't want DC value
    for (int i = 1; i < m_fft_fund_idx_range_min; ++i)
    {
        noise_rms += m_fft_modules[i];
    }

    for (int i = m_fft_fund_idx_range_max; i < fft_capture_size; ++i)
    {
        noise_rms += m_fft_modules[i];
    }

    noise_rms = to_rms(sqrt(noise_rms)) * inv_capture_size;

    double noise_db = linear_to_db(noise_rms);

    m_thdn = noise_rms / m_fft_rms;
    m_thddb = linear_to_db(m_thdn);
    m_thdn *= 100.0;
}

void AudioToolWindow::init_capture()
{
    const int capture_size = m_audiorecorder.get_buffer_size(float(m_recorder_latency_ms) / 1000.f, false);
    const int samplerate = m_audiorecorder.get_current_samplerate();
    if (capture_size == 0) return;

    destroy_capture();

    m_capture_size = capture_size;
    int fft_capture_size = capture_size / 2;

    m_wow_flutter_capture_size = samplerate / WOW_FLUTTER_DECIMATION * WOW_FLUTTER_ANALYSIS_TIME;
    int wow_capture_size = samplerate / WOW_FLUTTER_DECIMATION * (WOW_FLUTTER_ANALYSIS_TIME - 0.5f);
    int wow_start_capture = m_wow_flutter_capture_size - wow_capture_size;
    
    m_fftinl    = new double[capture_size];
    m_fftoutl   = new fftw_complex[capture_size];
    m_fftinr    = new double[capture_size];
    m_fftoutr   = new fftw_complex[capture_size];
    m_fftdrawl  = new double[fft_capture_size];
    m_fftdrawr  = new double[fft_capture_size];
    m_fftfreqs  = new double[fft_capture_size];   
    m_fft_modules   = new double[fft_capture_size];
    m_current_window_cache  = new double[capture_size];
    m_wow_complex_out       = new fftw_complex[wow_capture_size];

    m_fftwowdrawfreqs.resize(wow_capture_size/2); 
    m_fftdrawwow.resize(wow_capture_size/2);

    m_wow_flutter_data.resize(m_wow_flutter_capture_size);
    m_wow_flutter_data_x.resize(m_wow_flutter_capture_size);

    std::fill(m_wow_flutter_data.begin(), m_wow_flutter_data.end(), 0.);
    std::fill(m_fftdrawwow.begin(), m_fftdrawwow.end(), 0.);

    unsigned int fft_flags = FFTW_PRESERVE_INPUT;

    if (m_optimized_fft) fft_flags |= FFTW_MEASURE;
    else fft_flags |= FFTW_ESTIMATE;

    m_wow_data_mutex.lock();
    m_longterm_audio.clear();
    m_wow_data_mutex.unlock();

    m_fftplanr   = fftw_plan_dft_r2c_1d(capture_size, m_fftinr, m_fftoutr, fft_flags);
    m_fftplanl   = fftw_plan_dft_r2c_1d(capture_size, m_fftinl, m_fftoutl, fft_flags);
    m_fftplanwow = fftw_plan_dft_r2c_1d(wow_capture_size, &m_wow_flutter_data[wow_start_capture], m_wow_complex_out, fft_flags | FFTW_PRESERVE_INPUT);

    compute_fft_window_cache();
}

void AudioToolWindow::destroy_capture()
{
    // Wait WowAndFlutter thread to finish before releasing memory
    wait_wow_thread();

    if (m_fftplanr)   fftw_destroy_plan(m_fftplanr);
    if (m_fftplanl)   fftw_destroy_plan(m_fftplanl);
    if (m_fftplanwow) fftw_destroy_plan(m_fftplanwow);

    delete[] m_fftinl;
    delete[] m_fftoutl;
    delete[] m_fftinr;
    delete[] m_fftoutr;
    delete[] m_fftdrawl;
    delete[] m_fftdrawr;
    delete[] m_fftfreqs;
    delete[] m_wow_complex_out;
    delete[] m_fft_modules;
    delete[] m_current_window_cache;
    m_sound_data_x.clear();

    m_fftinl    = nullptr;
    m_fftoutl   = nullptr;
    m_fftinr    = nullptr;
    m_fftoutr   = nullptr;
    m_fftdrawl  = nullptr;
    m_fftdrawr  = nullptr;
    m_fftfreqs  = nullptr;
    m_fftplanr  = nullptr;
    m_fftplanl  = nullptr;
    m_fft_modules   = nullptr;
    m_fftplanwow = nullptr;
    m_wow_complex_out = nullptr;
    m_current_window_cache = nullptr;
}

void AudioToolWindow::reinit_recorder()
{
    m_audiorecorder.pause(true);

    if (m_audio_in_idx < 0) return;
    if (m_audiomanager.get_input_sample_rates(m_audio_in_idx).empty()) return;

    if (m_in_sample_rate_idx >= m_audiomanager.get_input_sample_rates(m_audio_in_idx).size())
    {
        m_in_sample_rate_idx = 0;
        log_message("Cannot set recorder samplerate to requested value");
    }

    int input_samplerate = m_audiomanager.get_input_sample_rates(m_audio_in_idx)[m_in_sample_rate_idx];

    if (m_audiorecorder.init(float(m_recorder_latency_ms) / 1000.f, m_audio_in_idx, input_samplerate))
    {
        if (m_audio_loopback_out_idx >= 0 && !m_audioloopback.set(input_samplerate, float(m_recorder_latency_ms)/1000.f, m_audio_loopback_out_idx, m_audiorecorder.get_channel_count())){
            log_message("Cannot start loopback interface, maybe input samplerate is not compatible, try a different one");
        }
    }

    init_capture();

    m_audiorecorder.pause(!m_compute_on);
    m_audioloopback.pause(!m_compute_on | !m_audio_loopback_on);
    m_audiorecorder.get_input_volume_range_db(m_input_volume_min, m_input_volume_max); 
    m_audiorecorder.set_input_gain_linear(db_to_linear(m_input_gain));
}

void AudioToolWindow::reset_signal_generator()
{
    if (m_audiomanager.get_output_sample_rates(m_audio_out_idx).empty()) return;

    if (m_out_sample_rate_idx >= m_audiomanager.get_output_sample_rates(m_audio_out_idx).size())
    {
        m_out_sample_rate_idx = 0;
        log_message("Cannot set player samplerate to requested value");
    }

    int output_device_samplerate = m_audiomanager.get_output_sample_rates(m_audio_out_idx)[m_out_sample_rate_idx];

    m_signal_generator.destroy();
    m_signal_generator.init(m_audio_out_idx, output_device_samplerate, m_signalgen_latency_s);
    m_signal_generator.set_pitch(m_signal_generator_pitch);
    m_signal_generator.start();
    m_signal_generator.pause(!m_signal_generator_switch);
    m_signal_generator.set_volume(m_signalgen_volume_db);
    m_signal_generator.set_hw_volume(db_to_linear(m_output_hw_volume_db));
}

void AudioToolWindow::process_sweep()
{
    const float current_sample_rate = m_audiorecorder.get_current_samplerate();
    const float fft_step = m_capture_size / current_sample_rate;
    double const* current_fft_draw = m_fft_channel_left ? m_fftdrawl : m_fftdrawr;

    if (!m_async_sweep)
    {
        bool need_stop_sweep = false;
        if (m_sweep_target_frequency > 24000)
        {
            need_stop_sweep = true;
        }

        int min_freq_idx = std::max(int((m_sweep_target_frequency - 500) * fft_step), 1);
        int max_freq_idx = std::min(int((m_sweep_target_frequency + 500) * fft_step), m_capture_size / 2);

        double max_val = m_noise_foor;
        for (int i = min_freq_idx; i < max_freq_idx; ++i)
        {
            if (current_fft_draw[i] > max_val)
                max_val = current_fft_draw[i];
        }

        m_sweep_values.push_back(max_val);
        m_sweep_freqs.push_back(m_sweep_target_frequency);

        double logfreq_min = log10(20.);
        double logfreq_max = log10(24000.);
        double step = (logfreq_max - logfreq_min) / m_sweep_capture_num;

        double newlogfreq = log10(m_sweep_target_frequency) + step;

        if (need_stop_sweep)
        {
            // We've reached the end of the measure
            stop_sweep_gen();
            return;
        }

        m_sweep_target_frequency = pow(10., newlogfreq);
        m_signal_generator.set_pitch(m_sweep_target_frequency, 0.01);
    }
    else
    {
        double found_fundamental = m_sweep_threshold_level;
        double found_frequency = -1;
        for (int i = 1; i < m_capture_size / 2; ++i)
        {
            if (current_fft_draw[i] > found_fundamental)
            {
                found_fundamental = current_fft_draw[i];
                found_frequency = double(i) / fft_step;
            }
        }
        if (found_frequency > 0)
        {
            bool found_bin = false;
            int sweep_values_index = 0;
            for (auto current_frequency : m_sweep_freqs)
            {
                double freq_low = current_frequency - (current_frequency * 0.1);
                double freq_hi = current_frequency + (current_frequency * 0.1);
                if (found_frequency > freq_low && found_frequency < freq_hi)
                {
                    if (m_sweep_values[sweep_values_index] < found_fundamental){
                        m_sweep_values[sweep_values_index] = found_fundamental;
                        found_bin = true;
                        break;
                    }
                }

                bool freq_already_in_list = std::find(m_sweep_freqs.begin(), m_sweep_freqs.end(), found_frequency) != m_sweep_freqs.end();
                if (freq_already_in_list){
                    // Found the same frequency again
                    if (m_sweep_values[sweep_values_index] < found_fundamental)
                    {
                        // Found a higher value for the same frequency, update it
                        m_sweep_values[sweep_values_index] = found_fundamental;
                    }
                    found_bin = true;
                    break;
                }
                if (current_frequency > found_frequency && sweep_values_index >= 0)
                {
                    m_sweep_values.insert(m_sweep_values.begin() + sweep_values_index, found_fundamental);
                    m_sweep_freqs.insert(m_sweep_freqs.begin() + sweep_values_index, found_frequency);
                    found_bin = true;
                    break;
                }
                sweep_values_index++;
            }
            if (!found_bin)
            {
                m_sweep_values.push_back(found_fundamental);
                m_sweep_freqs.push_back(found_frequency);
            }
            m_sweep_last_measure_freq = found_frequency > m_sweep_last_measure_freq ? found_frequency : m_sweep_last_measure_freq;
        }
    }
    m_sweep_timer_chrono.reset();
}