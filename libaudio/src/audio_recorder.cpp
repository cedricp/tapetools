#include "audio_recorder.h"
#include <utils.h>

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
    IringBuffer* rbuffer = ar->m_ring_buffer;

    int numSamplesToWrite = frameCount * ar->m_instreaminfo.numChannel;


    if (rbuffer->getWriteAvailable() < numSamplesToWrite)
    {
        return paContinue;
    }

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
    m_manager.safe_close_stream(m_instream);

    delete m_ring_buffer;
    m_ring_buffer = nullptr;
}

bool PAaudioRecorder::init(float latency, int device_idx, int samplerate)
{
    destroy();

    m_instream = m_manager.get_input_stream(samplerate, device_idx, latency, recordCallback, this, m_instreaminfo);
    
    if (!m_instream){
        return false;
    }

    bool fp = m_manager.get_is_floatingpoint();

    int bytes_per_sample = Pa_GetSampleSize(m_instreaminfo.format);
    int capacity = get_buffer_size(latency);
    if (fp) m_ring_buffer = new ringBuffer<float>(capacity*3);
    else m_ring_buffer = new ringBuffer<int16_t>(capacity*3);

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

    if(pause)
    {
        Pa_StopStream(m_instream);
    }
    else
    {
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

    bool fp = m_manager.get_is_floatingpoint();

    if (data.size() != size) data.resize(size, 0);

    if (fp)
    {
        m_ring_buffer->read(data.data(), size);
    }
    else
    {
        std::vector<int16_t> intdata(size);
        m_ring_buffer->read(intdata.data(), size);

        for (int i = 0; i < size; ++i)
        {
            data[i] = (float)intdata[i] / (float)INT16_MAX;
        }
    }

    return true;
}

float PAaudioRecorder::get_ringbuffer_occupation()
{
    if (m_ring_buffer == nullptr) return 0;
    return ((float)m_ring_buffer->getReadAvailable() / (float)m_ring_buffer->getBufferSize()) * 100.;
}

void PAaudioRecorder::set_input_gain_db(float gain)
{
    m_manager.set_device_mixer_volume_db(m_instreaminfo.deviceIndex, gain);
}

void PAaudioRecorder::set_input_gain_linear(float gain)
{
    m_manager.set_device_mixer_volume_scalar(m_instreaminfo.deviceIndex, gain);
}