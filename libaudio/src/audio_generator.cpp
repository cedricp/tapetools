#include <audio_generator.h>

void log_message(const char* format, ...);

int PAaudioWaveformGenerator::generator_callback(const void* input, void* output,
                    unsigned long frameCount,
                    const PaStreamCallbackTimeInfo* timeInfo,
                    PaStreamCallbackFlags statusFlags,
                    void* userData)
{
    static double smoothdata = 0;

    if (output == nullptr) return paContinue;

    PAaudioWaveformGenerator* udata = (PAaudioWaveformGenerator*)userData;
    const StreamInfo info = udata->get_info();
    double float_sample_rate = info.sampleRate;
    double seconds_per_frame = 1.0 / float_sample_rate;

    float* data = (float*)output;

    for (int i = 0; i < frameCount; ++i){
        double sample = 0;
        if (udata->m_mode == SINE)
        {
            sample = udata->m_sinewave.sine_wave_sample();
        }
        else if (udata->m_mode == WHITE_NOISE)
        {
            sample = udata->m_volume * (randq64_double()*2.0 -1.0);
        }
        else if (udata->m_mode == BROWN_NOISE)
        {
            sample = udata->m_volume * (randq64_double()*2.0 -1.0);
            sample = smoothdata - (0.025* (smoothdata - sample));
        }

        for (int channel = 0; channel < info.numChannel; channel ++) {
            *data++ = sample;
        }
    }

    return paContinue;
}

PAaudioWaveformGenerator::PAaudioWaveformGenerator()
{
    m_pitch = 1000;

}

PAaudioWaveformGenerator::~PAaudioWaveformGenerator()
{
    destroy();
}

void PAaudioWaveformGenerator::destroy()
{
    Pa_CloseStream(m_outstream);
    m_outstream = nullptr;
}

bool PAaudioWaveformGenerator::init(PAaudioManager& manager, int device_idx, int samplerate, float latency){
    m_pitch = 1000;

    if (!manager.valid()){
        log_message("audioSine::init : AudioManager not valid\n");
    }

    m_seconds_offset = 0;
    
    if (m_outstream){
        Pa_CloseStream(m_outstream);
        m_outstream = nullptr;
    }

    std::tie(m_outstream, m_outstreaminfo) = manager.get_output_stream(samplerate, device_idx, latency, paFloat32, generator_callback, this);

    if (m_outstream == nullptr){
        log_message("unable to open device index: %i\n", device_idx);
        return false;
    }


    m_sinewave.set(0, samplerate, 1.);

    return true;
}

int PAaudioWaveformGenerator::get_samplerate()
{
    return m_outstreaminfo.sampleRate;
}


bool PAaudioWaveformGenerator::start()
{
    if (m_outstream == nullptr){
        log_message("audioSine::start : outstream not initialized\n");
        return false;
    }
    
    PaError err = Pa_StartStream(m_outstream);
    return err == paNoError;
}

bool PAaudioWaveformGenerator::pause(bool pause)
{
    if (m_outstream == nullptr){
        log_message("audioSine::pause : outstream not initialized\n");
        return false;
    }
    if (pause){    
        PaError err = Pa_StopStream(m_outstream);
        return err == paNoError;
    } else {
        PaError err = Pa_StartStream(m_outstream);
        return err == paNoError;
    }
}

void PAaudioWaveformGenerator::set_pitch(double pitch, double duration)
{
    m_pitch = pitch;
    m_sinewave.sine_wave_frequency_transition(m_pitch, duration);
}

void PAaudioWaveformGenerator::set_volume(int db, double duration)
{
    m_volume = pow(10, (double)db/20);   
    m_sinewave.sine_wave_amplitude_transition(m_volume, duration); 
}