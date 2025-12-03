#pragma once

#include "audio_manager.h"

class PAaudioRecorder
{
    IringBuffer *m_ring_buffer = nullptr;
    PaStream* m_instream = nullptr;
    PAaudioManager& m_manager;
    StreamInfo m_instreaminfo;

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
    void destroy();

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

    void set_input_gain_db(float gain);
    void set_input_gain_linear(float gain);

    void get_input_volume_range_db(float &min_db, float &max_db){
        return m_manager.get_volume_range_db(m_instreaminfo.deviceIndex, min_db, max_db);
    }

};