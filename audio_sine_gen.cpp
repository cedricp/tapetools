#include "audio_sine_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static inline void write_sample(char *ptr, double sample) {
    int16_t *buf = (int16_t *)ptr;
    constexpr double range = (double)INT16_MAX - (double)INT16_MIN;
    double val = sample * range / 2.0;
    *buf = val;
}

void audioSineGenerator::write_callback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max){
    if (outstream == nullptr){
        return;
    }
    
    audioSineGenerator* udata = (audioSineGenerator*)outstream->userdata;
    double float_sample_rate = outstream->sample_rate;
    double seconds_per_frame = 1.0 / float_sample_rate;
    SoundIoChannelArea *areas;
    int err;
    
    static int old_pitch = -1;

    int frames_left = frame_count_max;
    double pitch = udata->m_pitch;

    for (;;) {
        int frame_count = frames_left;
        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            fprintf(stderr, "unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }

        if (!frame_count)
            break;

        double step = 0;
        if (old_pitch > 0){
            step = (pitch - old_pitch) / (double)frame_count;
        }

        const SoundIoChannelLayout *layout = &outstream->layout;
        for (int frame = 0; frame < frame_count; ++frame) {
            double radians_per_second = pitch * 2.0 * M_PI; 
            double sample = udata->m_volume * sin((udata->m_seconds_offset + frame * seconds_per_frame) * radians_per_second);
            for (int channel = 0; channel < layout->channel_count; channel += 1) {
                write_sample(areas[channel].ptr, sample);
                areas[channel].ptr += areas[channel].step;
            }
            pitch += step;
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
    old_pitch = udata->m_pitch;
}

void audioSineGenerator::error_callback(SoundIoOutStream *outstream, int err)
{
    audioSineGenerator *ar = (audioSineGenerator*)outstream->userdata;
    fprintf(stderr, "audioSineGenerator::error_callback %s\n", soundio_strerror(err));
}

void audioSineGenerator::underflow_callback(SoundIoOutStream *outstream) {
    static int count = 0;
    fprintf(stderr, "underflow %d\n", count++);
}

audioSineGenerator::audioSineGenerator(){
    m_outstream = nullptr;
    m_pitch = 440;
}

audioSineGenerator::~audioSineGenerator(){
    destroy();
}

bool audioSineGenerator::init(audioManager& manager, int device_idx, int samplerate, float latency){
    if (!manager.valid()){
        fprintf(stderr, "audioSine::init : AudioManager not valid\n");
    }

    m_seconds_offset = 0;
    
    if (m_outstream){
        manager.release_output_stream(m_outstream);
        m_outstream = nullptr;
    }

    m_outstream = manager.get_out_stream(latency, samplerate, SoundIoFormatS16NE, device_idx);

    if (m_outstream == nullptr){
        fprintf(stderr, "unable to open device index: %i\n", device_idx);
        return false;
    }

    m_outstream->userdata = (void*)this; 
    m_outstream->write_callback = this->write_callback;
    m_outstream->underflow_callback = this->underflow_callback;
    m_outstream->error_callback = this->error_callback;

    if (m_outstream->layout_error){
        fprintf(stderr, "unable to set channel layout: %s\n", soundio_strerror(m_outstream->layout_error));
        return false;
    }
    return true;
}

int audioSineGenerator::get_samplerate()
{
    return m_outstream->sample_rate;
}


void audioSineGenerator::destroy()
{
    if (m_outstream){
        soundio_outstream_clear_buffer(m_outstream);
        soundio_outstream_destroy(m_outstream);
        m_outstream = nullptr;
    }
}

bool audioSineGenerator::start()
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

bool audioSineGenerator::pause(bool pause)
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

void audioSineGenerator::set_pitch(double pitch)
{
    m_pitch = pitch;
}

void audioSineGenerator::set_volume(int db)
{
    m_volume = pow(10, (double)db/20);    
}


