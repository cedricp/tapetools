#pragma once

#include "audio_manager.h"

class audioSineGenerator{
    double m_seconds_offset = 0;
    double m_pitch;
    SoundIoOutStream *m_outstream = nullptr;

    static void write_callback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max);
    static void underflow_callback(SoundIoOutStream *outstream);

public:
    audioSineGenerator();
    ~audioSineGenerator();

    void destroy();
    bool init(audioManager& manager, int device_idx, int samplerate);
    bool start();
    bool pause(bool pause = true);

    int get_samplerate();

    void setPitch(double pitch);
};