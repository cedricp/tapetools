#pragma once

#include "audio_manager.h"
#include "callback.h"


class audioRecorder
{
    SoundIoInStream *m_instream = nullptr;
    ringBuffer *m_ring_buffer = nullptr;
    bool m_32bits_sampling = true;
    int m_actual_capacity = 0;
    static void read_callback(SoundIoInStream *instream, int frame_count_min, int frame_count_max);
    static void overflow_callback(SoundIoInStream *instream);
    static void error_callback(SoundIoInStream *instream, int err);
    audioManager& m_manager;
    void destroy();
public:
    UserEvent buffer_full_event;
    audioRecorder(audioManager& audioManager);
    ~audioRecorder();

    bool init(float latency, int device_idx, int samplerate);
    int get_available_bytes();
    int get_available_samples();
    bool start();
    bool pause(bool);

    void get_data(std::vector<double>& data, size_t size);
    int  get_current_samplerate();
    int  get_channel_count();

    int get_buffer_size(float time, bool channels_mult = true);
};