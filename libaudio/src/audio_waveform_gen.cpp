#define _USE_MATH_DEFINES

#include "audio_waveform_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static bool is_seed_init = false;
static uint64_t u,v,w;

static inline uint64_t randq64_uint64()
{    
    if (!is_seed_init){
        is_seed_init = true;
        v = 4101842887655102017LL;
        w = 1;
        u = 1ULL ^ v; 
        randq64_uint64();
        v = u; 
        randq64_uint64();
        w = v; 
        randq64_uint64();
    }
    
	u = u * 2862933555777941757LL + 7046029254386353087LL;
	v ^= v >> 17; 
    v ^= v << 31; 
    v ^= v >> 8;
	w = 4294957665U*(w & 0xffffffff) + (w >> 32);
	uint64_t x = u ^ (u << 21); 
    x ^= x >> 35; 
    x ^= x << 4;
	return (x + v) ^ w;
}

static inline double randq64_double()
{
    return 5.42101086242752217E-20 * randq64_uint64();
}

static inline void write_sample_16bits(char *ptr, double sample) {
    int16_t *buf = (int16_t *)ptr;
    constexpr double range = (double)INT16_MAX - (double)INT16_MIN;
    double val = sample * range / 2.0;
    *buf = val;
}

static inline void write_sample_float_32bits(char *ptr, double sample) {
    float *buf = (float *)ptr;
    *buf = sample;
}

const double 
    radians = (M_PI * 2);


void audioWaveformGenerator::write_callback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max){
    static double smoothdata = 0;

    if (outstream == nullptr){
        return;
    }
    
    audioWaveformGenerator* udata = (audioWaveformGenerator*)outstream->userdata;
    double float_sample_rate = outstream->sample_rate;
    double seconds_per_frame = 1.0 / float_sample_rate;
    SoundIoChannelArea *areas;
    int err;
    
    int frames_left = frame_count_max;
    
    for (;;) {
        int frame_count = frames_left;
        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            fprintf(stderr, "unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }
        
        if (!frame_count)
        break;
        
        const SoundIoChannelLayout *layout = &outstream->layout;
        double t_sweep = frame_count / float_sample_rate;
        for (int frame = 0; frame < frame_count; ++frame)
        {
            double sample = 0;
            if (udata->m_mode == SINE)
            {
                double curr_time = udata->m_seconds_offset + (frame * seconds_per_frame);
                double fm_test = udata->m_fm_freq > 0 ? sin(2.0 * M_PI * udata->m_fm_freq * curr_time) / udata->m_fm_freq * udata->m_fm_strength : 0;
                sample = udata->m_sinewave.sine_wave_sample() + fm_test;
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

            for (int channel = 0; channel < layout->channel_count; channel += 1) {
                write_sample_float_32bits(areas[channel].ptr, sample);
                areas[channel].ptr += areas[channel].step;
            }
        }
        udata->m_seconds_offset = fmod(udata->m_seconds_offset + seconds_per_frame * frame_count, 1.0);

        if ((err = soundio_outstream_end_write(outstream))) {
            if (err == SoundIoErrorUnderflow)
                return;
            fprintf(stderr, "unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }

        frames_left -= frame_count;
        if (frames_left <= 0)
            break;
    }
}

void audioWaveformGenerator::error_callback(SoundIoOutStream *outstream, int err)
{
    audioWaveformGenerator *ar = (audioWaveformGenerator*)outstream->userdata;
    fprintf(stderr, "audioSineGenerator::error_callback %s\n", soundio_strerror(err));
}

void audioWaveformGenerator::underflow_callback(SoundIoOutStream *outstream) {
    static int count = 0;
    fprintf(stderr, "underflow %d\n", count++);
}

audioWaveformGenerator::audioWaveformGenerator(){
    m_outstream = nullptr;
    m_pitch = 1000;
}

audioWaveformGenerator::~audioWaveformGenerator(){
    destroy();
}

bool audioWaveformGenerator::init(audioManager& manager, int device_idx, int samplerate, float latency){
    m_pitch = 1000;

    if (!manager.valid()){
        fprintf(stderr, "audioSine::init : AudioManager not valid\n");
    }

    m_seconds_offset = 0;
    
    if (m_outstream){
        manager.release_output_stream(m_outstream);
        m_outstream = nullptr;
    }

    m_outstream = manager.get_out_stream(latency, samplerate, SoundIoFormatFloat32NE, device_idx);

    if (m_outstream == nullptr){
        fprintf(stderr, "unable to open device index: %i\n", device_idx);
        return false;
    }

    m_outstream->userdata = (void*)this; 
    m_outstream->write_callback = this->write_callback;
    m_outstream->underflow_callback = this->underflow_callback;
    m_outstream->error_callback = this->error_callback;

    m_sinewave.set(0, samplerate, 1.);

    return true;
}

int audioWaveformGenerator::get_samplerate()
{
    return m_outstream->sample_rate;
}


void audioWaveformGenerator::destroy()
{
    if (m_outstream){
        soundio_outstream_clear_buffer(m_outstream);
        soundio_outstream_destroy(m_outstream);
        m_outstream = nullptr;
    }
}

bool audioWaveformGenerator::start()
{
    if (m_outstream == nullptr){
        fprintf(stderr, "audioSine::start : outstream not initialized\n");
        return false;
    }
    int err;
    if ((err = soundio_outstream_start(m_outstream))){
        fprintf(stderr, "unable to start device: %s\n", soundio_strerror(err));
        return false;
    }
    return true;
}

bool audioWaveformGenerator::pause(bool pause)
{
    if (m_outstream == nullptr){
        fprintf(stderr, "audioSine::pause : outstream not initialized\n");
        return false;
    }

    int err;
    if ((err = soundio_outstream_pause(m_outstream, pause))) {
        fprintf(stderr, "unable to pause device: %s\n", soundio_strerror(err));
        return false;
    }
    return true;
}

void audioWaveformGenerator::set_pitch(double pitch)
{
    m_pitch = pitch;
    m_sinewave.sine_wave_frequency_transition(m_pitch, .1);
}

void audioWaveformGenerator::set_volume(int db)
{
    m_volume = pow(10, (double)db/20);   
    m_sinewave.sine_wave_amplitude_transition(m_volume, .1); 
}


