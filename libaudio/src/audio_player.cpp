#include "audio_player.h"

void audioPlayer::write_callback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max)
{
    audioPlayer* udata = (audioPlayer*)outstream->userdata;

    if (outstream == nullptr){
        return;
    }

    if (udata->m_ringbuffer == nullptr){
        return;
    }

    if (udata->m_ringbuffer->capacity() == 0){
        return;
    }
    
    SoundIoChannelArea *areas;
    int err;
    
    int frames_left = frame_count_max;

    int16_t *read_ptr = (int16_t*)udata->m_ringbuffer->read_ptr();

    for (;;) {
        int frame_count = frames_left;
        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            fprintf(stderr, "unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }

        if (!frame_count)
            break;

        const SoundIoChannelLayout *layout = &outstream->layout;
        const int channelcount = layout->channel_count;
        int16_t last_sample = 0;
        for (int frame = 0; frame < frame_count; ++frame) {

            for (int channel = 0; channel < channelcount; channel += 1) {
                if (udata->m_ringbuffer->fill_count() <= 0) continue;
                
                if (channel < udata->m_numchannels){
                    last_sample = *read_ptr++;
                    *(int16_t*)areas[channel].ptr = last_sample;
                    udata->m_ringbuffer->advance_read_ptr(sizeof(uint16_t));
                } else {
                    *(int16_t*)areas[channel].ptr = last_sample;
                }
                areas[channel].ptr += areas[channel].step;
            }
        }

        if ((err = soundio_outstream_end_write(outstream))) {
            if (err == SoundIoErrorUnderflow)
            {
                return;
            }
            fprintf(stderr, "unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }

        frames_left -= frame_count;
        if (frames_left <= 0)
            break;
    }
}
void audioPlayer::underflow_callback(SoundIoOutStream *outstream)
{
    fprintf(stderr, "underflow\n");
}
void audioPlayer::error_callback(SoundIoOutStream *outstream, int err)
{
    fprintf(stderr, "audioSineGenerator::error_callback %s\n", soundio_strerror(err));
}

bool audioPlayer::set(int samplerate, float latency, int device_idx, int channels)
{
    destroy();

    m_numchannels = channels;
    m_ringbugger_size = samplerate * latency * channels * sizeof(int16_t) * 2;
    int err;

    if (m_outstream == nullptr)
    {
        m_outstream = m_manager.get_out_stream(latency, samplerate, SoundIoFormatS16NE, device_idx);
        
        if (!m_outstream)
        {
            fprintf(stderr, "unable to create output stream:");
            return false;
        }

        m_ringbuffer = m_manager.get_new_ringbuffer(m_ringbugger_size);

        m_outstream->write_callback = this->write_callback;
        m_outstream->underflow_callback = this->underflow_callback;
        m_outstream->error_callback = this->error_callback;
        m_outstream->userdata = this;

        return true;
    }
    return false;
}

bool audioPlayer::add_data(const int16_t data[], int size)
{
    int err;
    int recv_bytes_size = size * sizeof(int16_t);

    if (m_ringbuffer)
    {
        int16_t *write_ptr = (int16_t*)m_ringbuffer->write_ptr();
        int free_bytes  = m_ringbuffer->free_count();
        int added_bytes = 0;

        bool enough_space = true;
        if (recv_bytes_size > free_bytes)
        {
            recv_bytes_size = free_bytes;
            enough_space = false;
        }

        while(recv_bytes_size)
        {
            *write_ptr++ = *data++;
            recv_bytes_size-=sizeof(int16_t);
            added_bytes+=sizeof(int16_t);
        }
        
        m_ringbuffer->advance_write_ptr(added_bytes);

        if (!m_playing)
        {
            if ((err = soundio_outstream_start(m_outstream))){
                fprintf(stderr, "unable to start device: %s\n", soundio_strerror(err));
                return false;
            }   
            m_playing = true;
        }

        return enough_space;        
    }
    return false;
}

void audioPlayer::pause(bool pause)
{
    if(m_outstream)
    {
        soundio_outstream_pause(m_outstream, pause);
        if (pause)
        {
            // reset ringbuffer
            delete m_ringbuffer;
            m_ringbuffer = m_manager.get_new_ringbuffer(m_ringbugger_size);
        }
    }
}

void audioPlayer::destroy()
{
    if (m_outstream)
    {
        soundio_outstream_destroy(m_outstream);
        m_outstream = nullptr;
    }
    if (m_ringbuffer)
    {
        delete m_ringbuffer;
        m_ringbuffer = nullptr;
    }
    m_playing = false;
    m_numchannels = 0;
}