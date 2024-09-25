#define _USE_MATH_DEFINES
#include "main_widget.h"
#include "wow_flutter_thread.h"
#include <complex>

void AudioToolWindow::compute_fft_window_cache()
{
    if (m_current_window_cache != nullptr) delete[] m_current_window_cache;
    m_current_window_cache = new double[m_capture_size];

    for(int i = 0; i < m_capture_size; ++i) m_current_window_cache[i] = m_window_fn(i, m_capture_size);
}

void AudioToolWindow::compute_fft_window_corrections(int num_samples)
{
    int tmp = m_fft_window_fn_index;
    double inv_num_samples = 1. / num_samples;
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

bool AudioToolWindow::compute(bool compute_fft, bool compute_noise_floor)
{
    const int channelcount = m_audiorecorder.get_channel_count();
    if (channelcount == 0)
    {
        return false;
    }

    bool data_available = m_audiorecorder.get_data(m_raw_buffer, m_capture_size * channelcount);
    if (!data_available){
        return false;
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
    double twopif_over_sr = 2. * M_PI / current_sample_rate;

    // Fill audio waveform
    for (int i = 0; i < m_capture_size; i++)
    {
        //double sound_data = .3 * sin(3145.*double(i) * twopif_over_sr);//
        double sound_data = m_raw_buffer[i*channelcount] * m_audio_gain;
        m_sound_data1[i] = sound_data;
            
        // THD test for non linear signal by applying small odd harmomics distortion
        m_fftinl[i] = sound_data * m_current_window_cache[i];
        m_sound_data_x[i] = float(i) * inv_current_sample_rate * 1000.0;

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
    
    if (compute_fft)
    {
        double* current_fft_draw = m_fft_channel_left  ? m_fftdrawl : m_fftdrawr;
        // Compute and fill audio FFT
        ::fftw_execute(m_fftplanl);
        if (channelcount > 1) ::fftw_execute(m_fftplanr);

        std::vector<double> fftdatal(fft_capture_size);
        float sum = 0;
        for (int i = 0; i < fft_capture_size; ++i)
        {
            m_fftfreqs[i] = fft_step * (double)(i);
            double fftout = sqrt(m_fftoutl[i][0] * m_fftoutl[i][0] + m_fftoutl[i][1] * m_fftoutl[i][1]) * inv_fft_capture_size;
            fftout *= m_window_amplitude_correction[m_fft_window_fn_index];
            fftout = std::max(20.0 * log10(fftout), -200.0);
            m_fftdrawl[i] = isnan(fftout) ? -200.f : fftout;
            if (m_fft_channel_left) sum += fftout;

            if (channelcount > 1)
            {
                double fftout = sqrt(m_fftoutr[i][0] * m_fftoutr[i][0] + m_fftoutr[i][1] * m_fftoutr[i][1]) * inv_fft_capture_size;
                fftout *= m_window_amplitude_correction[m_fft_window_fn_index];
                fftout = std::max(20.0 * log10(fftout), -200.0);
                m_fftdrawr[i] = isnan(fftout) ? -200.f : fftout;
                if (m_fft_channel_right) sum += fftout;
            }
        }

        if (compute_noise_floor)
        {
            double mean = sum * inv_fft_capture_size;
            double stddev = 0;
            for (int i = 0; i < fft_capture_size; ++i)
            {
                double a = (current_fft_draw[i] - mean);
                stddev += a * a;
            }
            stddev = sqrt(stddev / float(fft_capture_size - 1));
            m_noise_foor = mean + stddev;
        } // compute_noise_floor
    } // compute_fft

    return true;
}


void AudioToolWindow::compute_wow_and_flutter()
{
    double samplerate = m_audiorecorder.get_current_samplerate();

            // Append captured audio data to get them
    int audio_capture_length = WOW_FLUTTER_ANALYSIS_TIME * samplerate;
    int sampled_audio_length = m_sound_data1.size();

    m_wow_data_mutex.lock();
    if (m_longterm_audio.empty())
    {
        m_longterm_audio.reserve(audio_capture_length);
    }

    if (m_longterm_audio.size() < audio_capture_length)
    {
        m_longterm_audio.insert(m_longterm_audio.end(), m_sound_data1.begin(), m_sound_data1.end());
    }
    else
    {
        int move_size = audio_capture_length - sampled_audio_length;
        memcpy(&m_longterm_audio[0], &m_longterm_audio[sampled_audio_length], move_size*sizeof(double));
        memcpy(&m_longterm_audio[move_size], &m_sound_data1[0], sampled_audio_length*sizeof(double));
    }
    m_wow_data_mutex.unlock();

    if (m_longterm_audio.size() < audio_capture_length)
    {
        // Wait buffer to be filled
        return;
    }

    App_SDL::get()->release_finished_threads();
    if(App_SDL::get()->get_thread("WFtask")){
        // Check is previous thread has terminated, if not, reject these samples to not overload
        return;
    }

    int reference_frequency = 3000;
    if (m_wow_test_frequency == 1) reference_frequency = 3150;
    else if (m_wow_test_frequency == 2) reference_frequency = m_wow_test_frequency_custom;

    // Launch thread
    WowAndFluterThread* wt = new WowAndFluterThread(samplerate, m_longterm_audio,
        m_wow_flutter_data, m_wow_flutter_data_x, m_fft_channel_left ? m_sound_data1 : m_sound_data2, reference_frequency,
        m_wow_data_mutex, WOW_FLUTTER_ANALYSIS_TIME, m_wf_filter_freq_combo,
        m_wow_peak_detection, m_wow_mean, WOW_FLUTTER_DECIMATION, m_fftdrawwow, m_fftoutwow, m_fftwowdrawfreqs, m_fftplanwow);
    wt->start();
}

void AudioToolWindow::compute_channels_phase()
{
    if (m_audiorecorder.get_channel_count() < 2) return;

    float fft_capture_size = m_capture_size / 2;

    // Get the fundamental frequency FFT result
    fftw_complex* right_comp = &(m_fftoutl[m_fft_highest_idx[m_fundamental_index]]);
    fftw_complex* left_comp = &(m_fftoutr[m_fft_highest_idx[m_fundamental_index]]);

    // Compute the phase (complex argument) of left and right channels
    double right_phase = wrap_phase(atan2((*right_comp)[1], (*right_comp)[0]));
    double left_phase = wrap_phase(atan2((*left_comp)[1], (*left_comp)[0]));
    // Compute the phase difference and convert to degrees
    m_phase_diff_degrees = wrap_phase(right_phase - left_phase) * 180. / M_PI;

    // Compute amplitude difference (diff of complex modules)
    double left_amplitude  = sqrt( ( (*left_comp)[0] * (*left_comp)[0] ) + ( (*left_comp)[1] * (*left_comp)[1]) ) / fft_capture_size;
    double right_amplitude = sqrt( ( (*right_comp)[0] * (*right_comp)[0] ) + ( (*right_comp)[1] * (*right_comp)[1]) ) / fft_capture_size;

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
    double* current_fft_draw = m_fft_channel_left ? m_fftdrawl : m_fftdrawr;
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

    m_fundamental_index = 0;

    int fundamental_frequency = m_fftfreqs[fundamental_index]; 

    m_fft_highest_idx[0] = fundamental_index;
    m_fft_highest_pos[0] = m_fftfreqs[fundamental_index];

    for (int i = 1; i < 8; ++i)
    {
        int i_order_harmonic = fundamental_index * (i+1);
        if (i_order_harmonic > fft_capture_size) break;
        m_fft_found_peaks++;

        m_fft_highest_idx[i] = i_order_harmonic;
        m_fft_highest_pos[i] = m_fftfreqs[i_order_harmonic];
    }

    // Compute Total Harmonic Distortion
    // Source http://www.r-type.org/addtext/add183.htm
    if (m_fft_found_peaks)
    {
        m_thd = 0;
        double fundamental_db = current_fft_draw[m_fft_highest_idx[0]];
        double totdbc = 0;
        for (int i = 1; i < m_fft_found_peaks; ++i)
        {
            double dBc = current_fft_draw[m_fft_highest_idx[i]] - fundamental_db;
            totdbc += pow(10.0, dBc / 10.0);
        }

        m_thd = sqrt(totdbc) * 100.;
    }
}

void AudioToolWindow::compute_thdn()
{
    int fft_capture_size = m_capture_size/2;
    fftw_complex * current_fft = m_fft_channel_left ? m_fftoutl : m_fftoutr;
    const double invsqrt2 = 1.0 / sqrt(2.0);
    const double inv_capture_size = 1.0 / (double(fft_capture_size));

    m_thdn = m_thddb = 0.;

    double max_val = -200;
    int max_val_index = 0;

    m_fft_rms = 0;
    for (int i = 1; i < fft_capture_size; ++i)
    {
        double fft_module = sqrt(current_fft[i][0] * current_fft[i][0] + current_fft[i][1] * current_fft[i][1]);
        fft_module *= m_window_energy_correction[m_fft_window_fn_index];
        fft_module *= fft_module;
        m_fft_rms += fft_module;
        m_rms_fft[i] = fft_module;
        
        if (fft_module > max_val)
        {
            max_val = fft_module;
            max_val_index = i;
        }
    }
    m_fft_rms = sqrt(m_fft_rms) * invsqrt2 * inv_capture_size;
    //printf("RMS = %f\n", total_rms);

    // Find FFT fundamental range
    double tmp = max_val;
    for (int i = max_val_index; i < fft_capture_size; ++i)
    {
        if (m_rms_fft[i] > tmp)
        {
            m_fft_fund_idx_range_max = i;
            break;
        }
        tmp = m_rms_fft[i];
    }
    
    tmp = max_val;
    for (int i= max_val_index; i >= 0; --i)
    {
        if (m_rms_fft[i] > tmp)
        {
            m_fft_fund_idx_range_min = i;
            break;
        }
        tmp = m_rms_fft[i];
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
        noise_rms += m_rms_fft[i];
    }

    for (int i = m_fft_fund_idx_range_max; i < fft_capture_size; ++i)
    {
        noise_rms += m_rms_fft[i];
    }

    noise_rms = sqrt(noise_rms) * invsqrt2 * inv_capture_size;

    double noise_db = 20. * log10(noise_rms);

    m_thdn = noise_rms / m_fft_rms;
    m_thddb = 20.0 * log10(m_thdn);
    m_thdn *= 100.0;
}

void AudioToolWindow::init_capture()
{
    int capture_size = m_audiorecorder.get_buffer_size(float(m_recorder_latency_ms) / 1000.f, false);
    int samplerate = m_audiorecorder.get_current_samplerate();
    if (capture_size == 0) return;
    
    destroy_capture();
    m_capture_size = capture_size;
    m_wow_flutter_capture_size = samplerate / WOW_FLUTTER_DECIMATION * WOW_FLUTTER_ANALYSIS_TIME;
    int fft_capture_size = capture_size / 2;
    int wow_fft_capture_size = m_wow_flutter_capture_size / 2;
    
    m_fftinl = new double[capture_size];
    m_fftoutl = new fftw_complex[capture_size];
    m_fftinr = new double[capture_size];
    m_fftoutr = new fftw_complex[capture_size];
    m_fftdrawl = new double[fft_capture_size];
    m_fftdrawr = new double[fft_capture_size];
    m_fftfreqs = new double[fft_capture_size];   
    m_rms_fft = new double[fft_capture_size];
    m_current_window_cache = new double[capture_size];

    m_fftwowdrawfreqs = new double[wow_fft_capture_size]; 
    m_fftdrawwow = new double[wow_fft_capture_size];

    m_fftoutwow = new fftw_complex[m_wow_flutter_capture_size];

    m_wow_flutter_data.resize(m_wow_flutter_capture_size);
    m_wow_flutter_data_x.resize(m_wow_flutter_capture_size);

    int fft_flags = FFTW_PRESERVE_INPUT;

    if (m_optimized_fft) fft_flags |= FFTW_MEASURE;
    else fft_flags |= FFTW_ESTIMATE;

    m_wow_data_mutex.lock();
    m_longterm_audio.clear();
    m_wow_data_mutex.unlock();

    m_fftplanr   = fftw_plan_dft_r2c_1d(capture_size, m_fftinr, m_fftoutr, fft_flags);
    m_fftplanl   = fftw_plan_dft_r2c_1d(capture_size, m_fftinl, m_fftoutl, fft_flags);
    m_fftplanwow = fftw_plan_dft_r2c_1d(m_wow_flutter_capture_size, m_wow_flutter_data.data(), m_fftoutwow, fft_flags | FFTW_PRESERVE_INPUT);

    compute_fft_window_cache();
}

void AudioToolWindow::reinit_recorder()
{
    if (m_audio_in_idx < 0) return;
    if (m_audiomanager.get_input_sample_rates(m_audio_in_idx).empty()) return;

    if (m_in_sample_rate_idx >= m_audiomanager.get_input_sample_rates(m_audio_in_idx).size())
    {
        m_in_sample_rate_idx = 0;
        printf("Cannot set recorder samplerate to requested value\n");
    }
    int samplerate = m_audiomanager.get_input_sample_rates(m_audio_in_idx)[m_in_sample_rate_idx];

    if (m_audiorecorder.init(float(m_recorder_latency_ms) / 1000.f, m_audio_in_idx, samplerate))
    {
        m_audiorecorder.start();
    }

    init_capture();

    m_audiorecorder.pause(!m_compute_on);
}

void AudioToolWindow::reset_sine_generator()
{
    if (m_audiomanager.get_output_sample_rates(m_audio_out_idx).empty()) return;

    if (m_out_sample_rate_idx >= m_audiomanager.get_output_sample_rates(m_audio_out_idx).size())
    {
        m_out_sample_rate_idx = 0;
        printf("Cannot set player samplerate to requested value\n");
    }
    int current_sine_samplerate = m_audiomanager.get_output_sample_rates(m_audio_out_idx)[m_out_sample_rate_idx];
    m_sine_generator.destroy();
    m_sine_generator.init(m_audiomanager, m_audio_out_idx, current_sine_samplerate, m_sinegen_latency_s);
    m_sine_generator.set_pitch(m_pitch);
    m_sine_generator.start();
    m_sine_generator.pause(!m_sine_generator_switch);
}