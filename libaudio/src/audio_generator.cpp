#include <audio_generator.h>
#include <utils.h>

void log_message(const char* format, ...);

int PAaudioWaveformGenerator::generator_callback(const void* input, void* output,
                    unsigned long frameCount,
                    const PaStreamCallbackTimeInfo* timeInfo,
                    PaStreamCallbackFlags statusFlags,
                    void* userData)
{
    if (output == nullptr) return paContinue;

    PAaudioWaveformGenerator* udata = (PAaudioWaveformGenerator*)userData;

    const StreamInfo info = udata->get_info();
    bool is_floatingpoint = (info.format == paFloat32);

    if (!udata->m_is_playing)
    {
        memset(output, 0, (is_floatingpoint ? sizeof(float) : sizeof(int16_t)) * frameCount * info.numChannel);
        return paAbort;
    }

    double float_sample_rate = info.sampleRate;
    double seconds_per_frame = 1.0 / float_sample_rate;

    int16_t* dataint = (int16_t*)output;
    float* datafloat = (float*)output;

    float volume = udata->m_volume;
    for (int i = 0; i < frameCount; ++i){
        double sample = 0;
        if (udata->m_mode == SINE)
        {
            sample = volume * udata->m_sinewave.sample();
        }
        else if (udata->m_mode == WHITE_NOISE)
        {
            sample = volume * udata->m_whitenoise.sample();
        }
        else if (udata->m_mode == BROWN_NOISE)
        {
            sample = volume * udata->m_brownnoise.sample();
        } else if (udata->m_mode == PINK_NOISE)
        {
            sample = volume * udata->m_pinknoise.sample();
        }

        for (int channel = 0; channel < info.numChannel; channel ++)
        {
            if (is_floatingpoint) *datafloat++ = (float)sample;
            else *dataint++ = (int16_t)(sample * INT16_MAX);
        }
    }

    return paContinue;
}

PAaudioWaveformGenerator::PAaudioWaveformGenerator(PAaudioManager& manager) : m_manager(manager)
{

}

PAaudioWaveformGenerator::~PAaudioWaveformGenerator()
{
    destroy();
}

void PAaudioWaveformGenerator::destroy()
{
    m_manager.safe_close_stream(m_outstream);
    m_outstream = nullptr;
}

bool PAaudioWaveformGenerator::init(int device_idx, int samplerate, float latency){
    m_pitch = 1000;

    if (!m_manager.valid()){
        log_message("audioSine::init : AudioManager not valid\n");
    }

    m_seconds_offset = 0;
    
    destroy();
    
    m_sinewave.set(0, samplerate, 1.);
    
    m_outstream = m_manager.get_output_stream(samplerate, device_idx, latency, generator_callback, this, m_outstreaminfo);

    if (m_outstream == nullptr){
        log_message("unable to open device index: %i\n", device_idx);
        return false;
    }



    return true;
}

int PAaudioWaveformGenerator::get_samplerate()
{
    return m_outstreaminfo.sampleRate;
}


bool PAaudioWaveformGenerator::start()
{
    return pause(false);
}

bool PAaudioWaveformGenerator::pause(bool pause)
{
    if (m_outstream == nullptr){
        log_message("PAaudioWaveformGenerator::pause : outstream not initialized\n");
        return false;
    }
    if (pause){
        m_is_playing = false;
        Pa_AbortStream(m_outstream);
        PaError err = Pa_StopStream(m_outstream);
        return err == paNoError;
    } else {
        m_is_playing = true;
        PaError err = Pa_StartStream(m_outstream);
        return err == paNoError;
    }
    return false;
}

void PAaudioWaveformGenerator::set_pitch(double pitch, double duration)
{
    m_pitch = pitch;
    m_sinewave.sine_wave_frequency_transition(m_pitch, duration);
}

void PAaudioWaveformGenerator::set_volume(int db, double duration)
{
    m_volume = db_to_linear(db);   
    m_sinewave.sine_wave_amplitude_transition(1.0, duration); 
}

void PAaudioWaveformGenerator::set_hw_volume(float vol)
{
    m_manager.set_device_mixer_volume_scalar(m_outstreaminfo.deviceIndex, vol);
}