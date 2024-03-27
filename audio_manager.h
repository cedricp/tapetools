#pragma once

#include <soundio.h>
#include <iostream>
#include <map>
#include <vector>

class audioManager
{
    SoundIoBackend m_backend = SoundIoBackendNone;
    SoundIoDevice* m_out_device = nullptr;
    SoundIoDevice* m_in_device = nullptr;
    SoundIo *m_soundio = nullptr;
    bool m_valid = false;

    std::vector<std::string> m_input_devices;
    std::vector<std::string> m_output_devices;
    std::vector<int> m_input_map;
    std::vector<int> m_output_map;
public:
    audioManager(SoundIoBackend backend = SoundIoBackendNone);
    ~audioManager();

    SoundIoOutStream*   get_out_stream(std::string stream_name, double latency, int sample_rate, SoundIoFormat format, int device_index = -1);
    SoundIoInStream*    get_in_stream(std::string stream_name, double latency, int sample_rate, SoundIoFormat format, int device_id = -1);

    SoundIoDevice*      get_output_device(){return m_out_device;}
    const std::vector<std::string>& get_input_devices(){return m_input_devices;}
    const std::vector<std::string>& get_output_devices(){return m_output_devices;}

    const std::vector<std::string> get_input_sample_rates_str();
    const std::vector<std::string> get_output_sample_rates_str();

    const std::vector<int> get_input_sample_rates();
    const std::vector<int> get_output_sample_rates();

    int get_sample_rate_by_index(int idx);

    int get_input_device_map(int idx){return m_input_map[idx];}
    int get_output_device_map(int idx){return m_output_map[idx];}

    int get_input_device_reverse_map(int mapid);
    int get_output_device_reverse_map(int mapid);

    void release_output_stream(SoundIoOutStream* ostream);
    void release_input_stream(SoundIoInStream* ostream);
    int  get_default_output_device_id();
    int  get_default_input_device_id();
    void flush();
    bool valid(){return m_valid;}
    SoundIo* get_soundio(){return m_soundio;}

};


