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
    if (m_outstream){
        Pa_AbortStream(m_outstream);
        Pa_CloseStream(m_outstream);
        m_outstream = nullptr;
    }
    delete m_ringbuffer;
    m_ringbuffer = nullptr;

    m_playing = false;
    m_numchannels = 0;
}

bool PAaudioLoopback::set(int samplerate, float latency, int device_idx, int channels)
{
    destroy();

    int ringbugger_size = samplerate * latency * m_outstreaminfo.numChannel * 4;
    
    std::tie(m_outstream, m_outstreaminfo) = m_manager.get_output_stream(samplerate, device_idx, latency, paFloat32, generator_callback, this, channels);

    m_ringbuffer = new ringBuffer(sizeof(float), ringbugger_size);

    return true;
}

bool PAaudioLoopback::add_data(const float data[], int size)
{
    if (m_ringbuffer && m_ringbuffer->getWriteAvailable() >= size){
        m_ringbuffer->write(data, size);
        return true;
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