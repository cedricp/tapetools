#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "audio_record.h"

audioRecorder::audioRecorder(audioManager& manager) : m_manager(manager)
{

}

audioRecorder::~audioRecorder()
{
    destroy();
}

void audioRecorder::destroy()
{
    if (m_instream){
        m_manager.release_input_stream(m_instream);
        m_instream = nullptr;
        fprintf(stderr, "audioRecorder::destroy stream %x\n", m_instream);
    }

    if (m_ring_buffer){
        soundio_ring_buffer_destroy(m_ring_buffer);
        m_ring_buffer = nullptr;
    }
}

bool audioRecorder::init(float latency, int device_idx, int samplerate)
{
    destroy();

    if (!m_manager.valid()){
        fprintf(stderr, "audioRecorder::init : AudioManager not valid\n");
        return false;
    }

    m_instream = m_manager.get_in_stream(latency, samplerate, SoundIoFormatS16NE, device_idx);
    fprintf(stderr, "audioRecorder::init : new stream %d\n", samplerate);
    if (m_instream == nullptr){
        return false;
    }

    m_instream->userdata = (void*)this; 
    m_instream->read_callback = this->read_callback;
    m_instream->overflow_callback = this->overflow_callback;
    m_instream->error_callback = this->error_callback;

    if (m_instream->layout_error){
        return false;
    }

    int capacity = get_buffer_size(latency) * sizeof(short);
    m_ring_buffer = m_manager.get_new_ringbuffer(capacity);
    return true;
}

void audioRecorder::overflow_callback(struct SoundIoInStream *instream)
{
    static int count = 0;
    fprintf(stderr, "overflow %d\n", ++count);
}

void audioRecorder::error_callback(struct SoundIoInStream *instream, int err)
{
    audioRecorder *ar = (audioRecorder*)instream->userdata;
    fprintf(stderr, "audioRecorder::error_callback %s\n", soundio_strerror(err));
    ar->m_instream = nullptr;
    soundio_ring_buffer_destroy(ar->m_ring_buffer);
    ar->m_ring_buffer = nullptr;
}

void audioRecorder::read_callback(SoundIoInStream *instream, int frame_count_min, int frame_count_max) {
    audioRecorder *ar = (audioRecorder*)instream->userdata;
    struct SoundIoChannelArea *areas;
    int err;

    char *write_ptr = soundio_ring_buffer_write_ptr(ar->m_ring_buffer);
    int free_bytes  = soundio_ring_buffer_free_count(ar->m_ring_buffer);
    int free_count  = free_bytes / instream->bytes_per_frame;

    if (free_count < frame_count_min) {
        fprintf(stderr, "ring buffer overflow\n");
        exit(1);
    }

    int write_frames = std::min(free_count, frame_count_max);
    int frames_left = write_frames;

    for (;;) {
        int frame_count = frames_left;

        if ((err = soundio_instream_begin_read(instream, &areas, &frame_count))) {
            fprintf(stderr, "begin read error: %s", soundio_strerror(err));
            exit(1);
        }

        if (!frame_count)
            break;

        if (!areas) {
            // Due to an overflow there is a hole. Fill the ring buffer with
            // silence for the size of the hole.
            memset(write_ptr, 0, frame_count * instream->bytes_per_frame);
        } else {
            for (int frame = 0; frame < frame_count; frame += 1) {
                for (int ch = 0; ch < instream->layout.channel_count; ch += 1) {
                    memcpy(write_ptr, areas[ch].ptr, instream->bytes_per_sample);
                    areas[ch].ptr += areas[ch].step;
                    write_ptr += instream->bytes_per_sample;
                }
            }
        }

        if ((err = soundio_instream_end_read(instream))) {
            fprintf(stderr, "end read error: %s", soundio_strerror(err));
            exit(1);
        }

        frames_left -= frame_count;
        if (frames_left <= 0)
            break;
    }

    int advance_bytes = write_frames * instream->bytes_per_frame;
    soundio_ring_buffer_advance_write_ptr(ar->m_ring_buffer, advance_bytes);
}

bool audioRecorder::start()
{
    int err;
    if ((err = soundio_instream_start(m_instream))) {
        fprintf(stderr, "unable to start input device: %s", soundio_strerror(err));
        return false;
    }
    return true;
}

bool audioRecorder::pause(bool p)
{
    int err;
    if ((err = soundio_instream_pause(m_instream, p))) {
        fprintf(stderr, "unable to pause input device: %s\n", soundio_strerror(err));
        return false;
    }
    return true;
}

void audioRecorder::get_data(std::vector<float>& data, size_t size)
{
    if (!m_ring_buffer){
        data.clear();
        return;
    }

    size_t fill_bytes = soundio_ring_buffer_fill_count(m_ring_buffer);
    int16_t *read_buf = (int16_t*)soundio_ring_buffer_read_ptr(m_ring_buffer);
    if (data.size() != size) data.resize(size);

    constexpr float inv16 = 1.0f / float(INT16_MAX);
    for (int i = 0; i < size; ++i){
        data[i] = float(read_buf[i]) * inv16;
    }
    soundio_ring_buffer_advance_read_ptr(m_ring_buffer, fill_bytes);
}

int audioRecorder::get_available_bytes()
{
    if (!m_ring_buffer){
        return 0;
    }
    return soundio_ring_buffer_fill_count(m_ring_buffer);
}

int audioRecorder::get_available_samples()
{
    if (!m_ring_buffer){
        return 0;
    }
    return soundio_ring_buffer_fill_count(m_ring_buffer) / m_instream->bytes_per_sample;
}

int audioRecorder::get_channel_count()
{
    if(m_instream==nullptr) return 0;
    return m_instream->layout.channel_count;
}


int audioRecorder::get_current_samplerate()
{
    if (m_instream == nullptr){
        return 0;
    }
    return m_instream->sample_rate;
}

int audioRecorder::get_buffer_size(float time, bool channels_mult)
{
    if (m_instream == nullptr){
        return 0;
    }
    return time * float(m_instream->sample_rate) * float(channels_mult ? get_channel_count() : 1.0f);
}