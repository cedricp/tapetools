#include "audio_recorder.h"

int PAaudioRecorder::recordCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData)
{
    // NumSamples = frameCount * numChannel
    float *in = (float*)inputBuffer;
    PAaudioRecorder* ar = (PAaudioRecorder*)userData;
    ringBuffer* rbuffer = ar->m_ring_buffer;

    if (inputBuffer == nullptr) return paContinue;

    int numSamplesToWrite = frameCount * ar->m_instreaminfo.numChannel;
    if (rbuffer->getWriteAvailable() < numSamplesToWrite) return paContinue;

    rbuffer->write(in, numSamplesToWrite);

    return paContinue;
}


PAaudioRecorder::PAaudioRecorder(PAaudioManager& audioManager) : m_manager(audioManager)
{
}

PAaudioRecorder::~PAaudioRecorder()
{
    destroy();
}

void PAaudioRecorder::destroy()
{
    if (m_instream){
        Pa_AbortStream(m_instream);
        Pa_CloseStream(m_instream);
        m_instream = nullptr;
    }

    delete m_ring_buffer;
    m_ring_buffer = nullptr;
}

bool PAaudioRecorder::init(float latency, int device_idx, int samplerate)
{
    destroy();

    std::tie(m_instream, m_instreaminfo) = m_manager.get_input_stream(samplerate, device_idx, latency, paFloat32, recordCallback, this);
    if (!m_instream){
        return false;
    }

    int bytes_per_sample = Pa_GetSampleSize(m_instreaminfo.format);
    int capacity = get_buffer_size(latency);
    m_ring_buffer = new ringBuffer(sizeof(float), capacity*2);

    return true;
}

int PAaudioRecorder::get_buffer_size(float time, bool channels_mult)
{
    if (m_instream == nullptr){
        return 0;
    }
    return time * float(m_instreaminfo.sampleRate) * float(channels_mult ? get_channel_count() : 1.0f);
}

int PAaudioRecorder::get_channel_count()
{
    if(m_instream==nullptr) return 0;
    return m_instreaminfo.numChannel;
}

int PAaudioRecorder::get_current_samplerate()
{
    if (m_instream == nullptr){
        return 0;
    }
    const PaStreamInfo* info = Pa_GetStreamInfo(m_instream);
    return info->sampleRate;
}

int PAaudioRecorder::get_available_bytes()
{
    if (!m_ring_buffer || m_instream == nullptr){
        return 0;
    }
    int bytes_per_sample = Pa_GetSampleSize(m_instreaminfo.format);
    return m_ring_buffer->getReadAvailable() * bytes_per_sample;
}

int PAaudioRecorder::get_available_samples()
{
    if (!m_ring_buffer || m_instream == nullptr){
        return 0;
    }
    return m_ring_buffer->getReadAvailable();
}

bool PAaudioRecorder::start()
{
    if (m_instream == nullptr){
        return false;
    }
    m_ring_buffer->flush();
    return Pa_StartStream(m_instream) == paNoError;
}

bool PAaudioRecorder::pause(bool pause)
{
    if (m_instream == nullptr){
        return false;
    }
    if(pause){
        Pa_StopStream(m_instream);
    } else {
        start();
    }
    return true;
}

bool PAaudioRecorder::get_data(std::vector<float>& data, size_t size)
{
    if (!m_ring_buffer){
        return false;
    }

    size_t sample_fill = m_ring_buffer->getReadAvailable();
    if (sample_fill < size){
        return false;
    }

    if (data.size() != size) data.resize(size, 0);

    m_ring_buffer->read(data.data(), size);

    return true;
}

float PAaudioRecorder::get_ringbuffer_occupation()
{
    if (m_ring_buffer == nullptr) return 0;
    return ((float)m_ring_buffer->getReadAvailable() / (float)m_ring_buffer->getBufferSize()) * 100.;
}