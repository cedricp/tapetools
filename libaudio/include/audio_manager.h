#pragma once

extern "C"{
#include <soundio.h>
}
#include <iostream>
#include <map>
#include <vector>
#include "callback.h"

class ringBuffer
{
    SoundIoRingBuffer* rb = nullptr;
public:
    ringBuffer(SoundIo* sio, int size){
        rb = soundio_ring_buffer_create(sio, size);
    }
    ~ringBuffer(){
        soundio_ring_buffer_destroy(rb);
    }
    char* write_ptr(){
        return soundio_ring_buffer_write_ptr(rb);
    }
    char* read_ptr(){
        return soundio_ring_buffer_read_ptr(rb);
    }
    void advance_read_ptr(int size){
        soundio_ring_buffer_advance_read_ptr(rb, size);
    }
    void advance_write_ptr(int size){
        soundio_ring_buffer_advance_write_ptr(rb, size);
    }
    int free_count(){
        return soundio_ring_buffer_free_count(rb);
    }
    int fill_count(){
        return soundio_ring_buffer_fill_count(rb);
    }
    int capacity(){
        return soundio_ring_buffer_capacity(rb);
    }
};                    


class audioManager
{
    SoundIoBackend m_backend = SoundIoBackendNone;
    SoundIoDevice* m_out_device = nullptr;
    SoundIoDevice* m_in_device = nullptr;
    SoundIo *m_soundio = nullptr;
    bool m_valid = false;
    std::string m_stream_name = "TapeTools";

    std::vector<std::string> m_input_devices;
    std::vector<std::string> m_output_devices;
    std::vector<int> m_input_map;
    std::vector<int> m_output_map;
    static void on_backend_disconnect(struct SoundIo *soundio, int err);
    static void on_device_change(struct SoundIo *soundio);
    void scan_devices();
public:
    UserEvent device_changed_event, backend_disconnected_event;
    
    audioManager(SoundIoBackend backend = SoundIoBackendNone);
    ~audioManager();

    SoundIoOutStream*   get_out_stream(double latency, int sample_rate, SoundIoFormat format, int device_index = -1);
    SoundIoInStream*    get_in_stream(double latency, int sample_rate, SoundIoFormat format, int device_id = -1);

    SoundIoDevice*      get_output_device(){return m_out_device;}
    const std::vector<std::string>& get_input_devices(){return m_input_devices;}
    const std::vector<std::string>& get_output_devices(){return m_output_devices;}

    const std::vector<std::string> get_input_sample_rates_str(int devidx);
    const std::vector<std::string> get_output_sample_rates_str(int devidx);

    const std::vector<int> get_input_sample_rates(int devidx);
    const std::vector<int> get_output_sample_rates(int devidx);

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

    ringBuffer* get_new_ringbuffer(int capacity);
};


