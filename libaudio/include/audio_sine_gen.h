#pragma once

#include "audio_manager.h"

class audioSineGenerator{
    double m_seconds_offset = 0;
    double m_pitch, m_oldpitch;
    double m_volume = 1.f;
    SoundIoOutStream *m_outstream = nullptr;

    static void write_callback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max);
    static void underflow_callback(SoundIoOutStream *outstream);
    static void error_callback(SoundIoOutStream *outstream, int err);
public:
    audioSineGenerator();
    ~audioSineGenerator();

    void destroy();
    bool init(audioManager& manager, int device_idx, int samplerate, float latency);
    bool start();
    bool pause(bool pause = true);

    int get_samplerate();

    void set_pitch(double pitch);
    void set_volume(int db);
};