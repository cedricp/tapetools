#pragma once

extern "C"{
#include <portaudio.h>
#include "ringbuffer.h"
}
#include <iostream>
#include <map>
#include <vector>
#include <tuple>
#include <algorithm>

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
    std::vector<int> m_input_map, m_output_map;

    bool m_use_exclusive_mode = false;
    bool m_pa_ok = false;

    int pa_to_input(int pa){
        return std::distance(m_input_map.begin(), std::find(m_input_map.begin(), m_input_map.end(), pa));
    }
    int pa_to_output(int pa){
        return std::distance(m_output_map.begin(), std::find(m_output_map.begin(), m_output_map.end(), pa));
    }
    int input_to_pa(int in){
        if (in >= m_input_map.size()) return paNoDevice;
        return m_input_map[in];
    }
    int output_to_pa(int in){
        if (in >= m_output_map.size()) return paNoDevice;
        return m_output_map[in];
    }

public:
    PAaudioManager();
    ~PAaudioManager();

    void set_exclusive_mode(bool mode){m_use_exclusive_mode = mode;}
    bool get_exclusive_mode(){return m_use_exclusive_mode;}

    std::tuple<PaStream*, StreamInfo> get_input_stream(int samplerate, int device_idx, float latency, PaSampleFormat format, PaStreamCallback* callback, void* userData);
    std::tuple<PaStream*, StreamInfo> get_output_stream(int samplerate, int device_idx, float latency, PaSampleFormat format, PaStreamCallback* callback, void* userData, int num_channels=-1);

    const std::vector<std::string>& get_input_devices(){return m_input_devices;}
    const std::vector<std::string>& get_output_devices(){return m_output_devices;}

    const std::vector<std::string> get_input_sample_rates_str(int devidx);
    const std::vector<std::string> get_output_sample_rates_str(int devidx);

    const std::vector<int> get_input_sample_rates(int devidx, bool only_default = false);
    const std::vector<int> get_output_sample_rates(int devidx, bool only_default = false);

    int  get_default_output_device_id();
    int  get_default_input_device_id();

    int get_default_input_device_samplerate();
    int get_default_output_device_samplerate();

    int get_default_input_device_samplerate_idx(int devidx);
    int get_default_output_device_samplerate_idx(int devidx);

    void scan_devices();

    ringBuffer* get_new_ringbuffer(int capacity);

    int get_default_input_samplerate_idx(int dev);
    int get_default_output_samplerate_idx(int dev);

    bool valid(){return m_pa_ok;}

    void flush(){
        scan_devices();
    }
};


