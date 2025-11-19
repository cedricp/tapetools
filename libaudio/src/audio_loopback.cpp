#include <audio_loopback.h>

int PAaudioLoopback::generator_callback(const void* input, void* output,
                    unsigned long frameCount,
                    const PaStreamCallbackTimeInfo* timeInfo,
                    PaStreamCallbackFlags statusFlags,
                    void* userData)
{
    // NumSamples = frameCount * numChannel
    if (output == nullptr) return paContinue;

    PAaudioLoopback* al = (PAaudioLoopback*)userData;
    IringBuffer* rbuffer = al->m_ringbuffer;
    int numChannel = al->m_outstreaminfo.numChannel;
    bool fp = al->m_outstreaminfo.format == paFloat32;

    int numSamples = numChannel * frameCount;

    if (rbuffer->getReadAvailable() >= numSamples){
        rbuffer->read(output, numSamples);
    } else {
        memset(output, 0, fp ? sizeof(float) : sizeof(int16_t));
    }

    return paContinue;
}


void PAaudioLoopback::destroy()
{
    m_manager.safe_close_stream(m_outstream);
    delete m_ringbuffer;
    m_ringbuffer = nullptr;

    m_playing = false;
    m_numchannels = 0;
}

bool PAaudioLoopback::set(int samplerate, float latency, int device_idx, int channels)
{
    destroy();

    m_outstream = m_manager.get_output_stream(samplerate, device_idx, latency, generator_callback, this, m_outstreaminfo, channels);

    int ringbuffer_size = samplerate * latency * m_outstreaminfo.numChannel * 2;

    if (!m_outstream){
        return false;
    }

    bool fp = (m_outstreaminfo.format == paFloat32);
    
    if (fp) m_ringbuffer = new ringBuffer<float>(ringbuffer_size);
    else m_ringbuffer    = new ringBuffer<int16_t>(ringbuffer_size);

    return true;
}

bool PAaudioLoopback::add_data(const float data[], int size)
{
    bool fp = m_outstreaminfo.format == paFloat32;

    if (m_ringbuffer && m_ringbuffer->getWriteAvailable() >= size){
        if (fp)
        {
            m_ringbuffer->write(data, size);
            return true;
        } else {
            std::vector<int16_t> intdata(size);
            for (int i = 0; i < size; ++i)
            {
                intdata[i] = data[i] * INT16_MAX;
            }
            m_ringbuffer->write(intdata.data(), size);
            return true;
        }
    }

    return false;
}

void PAaudioLoopback::pause(bool pause)
{
    if (!m_outstream) return;

    if (pause)
    {
        Pa_StopStream(m_outstream);
        m_ringbuffer->flush();
    }
    else
    {
        Pa_StartStream(m_outstream);
    }
}