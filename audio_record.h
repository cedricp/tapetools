#pragma once

#include "audio_manager.h"

class audioRecorder
{
    SoundIoInStream *m_instream = nullptr;
    SoundIoRingBuffer *m_ring_buffer = nullptr;
    static void read_callback(SoundIoInStream *instream, int frame_count_min, int frame_count_max);
    static void overflow_callback(SoundIoInStream *instream);
    audioManager& m_manager;
    void destroy();
public:
    audioRecorder(audioManager& audioManager);
    ~audioRecorder();

    bool init(float latency, int device_idx, int samplerate);
    int get_available_bytes();
    int get_available_samples();
    bool start();
    bool pause(bool);

    void get_data(std::vector<float>& data, size_t size);
    int  get_current_samplerate();
    int  get_channel_count();

    int get_buffer_size(float time, bool channels_mult = true);
};