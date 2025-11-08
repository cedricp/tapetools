#pragma once

extern "C"{
#include <portaudio.h>
#include "ringbuffer.h"
}
#include <iostream>
#include <map>
#include <vector>
#include <tuple>

struct StreamInfo
{
    int numChannel;
    int sampleRate;
    PaSampleFormat format; 
};

class PAaudioManager
{
    std::vector<std::string> m_input_devices;
    std::vector<std::string> m_output_devices;
    std::vector<int> m_input_map;
    std::vector<int> m_output_map;

    bool m_pa_ok = false;

public:
    PAaudioManager();
    ~PAaudioManager();

    std::tuple<PaStream*, StreamInfo> get_input_stream(int samplerate, int device_idx, float latency, PaSampleFormat format, PaStreamCallback* callback, void* userData);
    std::tuple<PaStream*, StreamInfo> get_output_stream(int samplerate, int device_idx, float latency, PaSampleFormat format, PaStreamCallback* callback, void* userData);

    const std::vector<std::string>& get_input_devices(){return m_input_devices;}
    const std::vector<std::string>& get_output_devices(){return m_output_devices;}

    const std::vector<std::string> get_input_sample_rates_str(int devidx);
    const std::vector<std::string> get_output_sample_rates_str(int devidx);

    const std::vector<int> get_input_sample_rates(int devidx);
    const std::vector<int> get_output_sample_rates(int devidx);

    int get_input_device_map(int idx){return m_input_map[idx];}
    int get_output_device_map(int idx){return m_output_map[idx];}

    int get_input_device_reverse_map(int mapid);
    int get_output_device_reverse_map(int mapid);

    int  get_default_output_device_id();
    int  get_default_input_device_id();

    void scan_devices();

    ringBuffer* get_new_ringbuffer(int capacity);

    bool valid(){return m_pa_ok;}

    void flush(){}
};


