#pragma once

#include "audio_manager.h"

class PAaudioRecorder
{
    ringBuffer *m_ring_buffer = nullptr;
    PaStream* m_instream = nullptr;
    PAaudioManager& m_manager;
    StreamInfo m_instreaminfo;
    void destroy();

    static int recordCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData);

public:
    PAaudioRecorder(PAaudioManager& audioManager);
    ~PAaudioRecorder();

    bool init(float latency, int device_idx, int samplerate);
    int get_available_bytes();
    int get_available_samples();
    bool start();
    bool pause(bool);

    bool get_data(std::vector<float>& data, size_t size);
    int  get_current_samplerate();
    int  get_channel_count();

    int get_buffer_size(float time, bool channels_mult = true);
    float get_ringbuffer_occupation();
};