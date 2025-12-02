#pragma once

#include "audio_manager.h"
#include <inttypes.h>

class PAaudioLoopback
{
    PAaudioManager& m_manager;
    PaStream *m_outstream = nullptr;
    StreamInfo m_outstreaminfo;
    IringBuffer* m_ringbuffer = nullptr;
    bool m_playing = false;

    static int generator_callback(const void* input, void* output,
                    unsigned long frameCount,
                    const PaStreamCallbackTimeInfo* timeInfo,
                    PaStreamCallbackFlags statusFlags,
                    void* userData);

public:
    PAaudioLoopback(PAaudioManager& manager) : m_manager(manager)
    {
    }
    ~PAaudioLoopback()
    {
        destroy();
    }
    void destroy();
    bool set(int samplerate, float latency, int device_idx, int channels);
    bool add_data(const float data[], int size);
    void pause(bool pause = true);
};