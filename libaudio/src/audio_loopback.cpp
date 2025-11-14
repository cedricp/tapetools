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
    ringBuffer* rbuffer = al->m_ringbuffer;
    int numChannel = al->m_outstreaminfo.numChannel;

    int numSamples = numChannel * frameCount;

    if (rbuffer->getReadAvailable() >= numSamples){
        rbuffer->read(output, numSamples);
    }

    return paContinue;
}


void PAaudioLoopback::destroy()
{
    m_manager.safe_close_stream(&m_outstream);
    delete m_ringbuffer;
    m_ringbuffer = nullptr;

    m_playing = false;
    m_numchannels = 0;
}

bool PAaudioLoopback::set(int samplerate, float latency, int device_idx, int channels)
{
    destroy();

    int ringbuffer_size = samplerate * latency * m_outstreaminfo.numChannel * 8;
    
    std::tie(m_outstream, m_outstreaminfo) = m_manager.get_output_stream(samplerate, device_idx, latency, generator_callback, this, channels);

    if (!m_outstream){
        return false;
    }

    bool fp = m_manager.get_is_floatingpoint();
    
    m_ringbuffer = new ringBuffer(fp ? sizeof(float) : sizeof(int16_t), ringbuffer_size);

    return true;
}

bool PAaudioLoopback::add_data(const float data[], int size)
{
    bool fp = m_manager.get_is_floatingpoint();
    if (m_ringbuffer && m_ringbuffer->getWriteAvailable() >= size){
    if (fp)
    {
        m_ringbuffer->write(data, size);
        return true;
    } else {
        std::vector<uint16_t> intdata(size);
        for (int i = 0; i < size; ++i)
        {
            intdata[i] = data[i] * INT16_MAX;
        }
        m_ringbuffer->write(intdata.data(), size);
    }
    }
    return false;
}

void PAaudioLoopback::pause(bool pause)
{
    if (!m_outstream) return;

    if (pause){
        Pa_StopStream(m_outstream);
        m_ringbuffer->flush();
    } else {
        Pa_StartStream(m_outstream);
    }
}