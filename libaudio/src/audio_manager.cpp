#include "audio_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static int g_samplerates[] = {
    192000,
    96000,
    88200,
    50400,
    50000,
    48000,
    47250,
    44100,
    44056,
    37800,
    32000,
    22050,
    16000,
    11025,
    8000
};

extern void log_message(const char* format, ...);

audioManager::audioManager(SoundIoBackend backend) : device_changed_event("audio_device_changed"), backend_disconnected_event("audio_backend_changed")
{
    m_backend = backend;
    m_out_device = nullptr;
    m_soundio = soundio_create();
    if (!m_soundio) {
        fprintf(stderr, "Cannot create soundio, aborting\n");
        abort();
    }

    m_soundio->userdata = this;
    m_soundio->on_backend_disconnect = this->on_backend_disconnect;
    m_soundio->on_devices_change = this->on_device_change;

    int err = (m_backend == SoundIoBackendNone) ? soundio_connect(m_soundio) : soundio_connect_backend(m_soundio, m_backend);

    if (err) {
        log_message("Unable to connect to backend: %s\n", soundio_strerror(err));
        return;
    }

    log_message("Backend: %s\n", soundio_backend_name(m_soundio->current_backend));

    flush();

    m_valid = true;
}

audioManager::~audioManager()
{
    if (m_out_device){
        soundio_device_unref(m_out_device);
    }
    if (m_in_device){
        soundio_device_unref(m_in_device);
    }
    if (m_soundio){
        soundio_destroy(m_soundio);
    }
}

void audioManager::on_backend_disconnect(struct SoundIo *soundio, int err)
{
    audioManager* am = (audioManager*)soundio->userdata;
    am->m_valid = false;
    am->backend_disconnected_event.push_delayed();
}

void audioManager::on_device_change(struct SoundIo *soundio)
{
    audioManager* am = (audioManager*)soundio->userdata;
    am->scan_devices();
    am->device_changed_event.push_delayed();
}

void audioManager::release_output_stream(SoundIoOutStream* ostream)
{
    soundio_outstream_destroy(ostream);
}

void audioManager::release_input_stream(SoundIoInStream* istream)
{
    soundio_instream_destroy(istream);
}

void audioManager::flush()
{
    soundio_flush_events(m_soundio);
}

void audioManager::scan_devices()
{
    m_output_devices.clear();
    m_output_map.clear();
    int output_count = soundio_output_device_count(m_soundio);
    for (int i = 0; i < output_count; ++i){
        struct SoundIoDevice *device = soundio_get_output_device(m_soundio, i);
        std::string device_name = device->name;
        if (device->is_raw) continue;
        m_output_devices.push_back(device_name);
        m_output_map.push_back(i);
        soundio_device_unref(device);
    }

    m_input_devices.clear();
    m_input_map.clear();
    int input_count = soundio_input_device_count(m_soundio);
    for (int i = 0; i < input_count; ++i){
        SoundIoDevice *device = soundio_get_input_device(m_soundio, i);
        std::string device_name = device->name;
        if (device->is_raw) continue;
        m_input_devices.push_back(device_name);
        m_input_map.push_back(i);
        soundio_device_unref(device);
    }
}

SoundIoOutStream* audioManager::get_out_stream(double latency, int sample_rate, SoundIoFormat format, int device_id)
{
    int err;

    if (m_out_device){
        soundio_device_unref(m_out_device);
    }

    if (device_id < 0){
        device_id = soundio_default_output_device_index(m_soundio);
    }
    log_message("Device index = %i", device_id);

    if (device_id < 0) {
        log_message("Output device not found");
        return nullptr;
    }

    m_out_device = soundio_get_output_device(m_soundio, device_id);
    if (!m_out_device) {
        log_message("out of memory");
        return nullptr;
    }

    log_message("Output device: %s", m_out_device->name);

    if (m_out_device->probe_error) {
        log_message("Cannot probe device: %s", soundio_strerror(m_out_device->probe_error));
        return nullptr;
    }

    SoundIoOutStream* ostream = soundio_outstream_create(m_out_device);

    if (!ostream) {
        log_message("out of memory");
        return nullptr;
    }

    if (sample_rate < 0){
        std::vector<int> sr = get_output_sample_rates(device_id);
        if (sr.empty()){
            return nullptr;
        }
        sample_rate = sr[0];
    }

    ostream->name = m_stream_name.c_str();
    ostream->software_latency = latency;
    ostream->sample_rate = sample_rate;
    ostream->format = format;

    if ((err = soundio_outstream_open(ostream))) {
        log_message("unable to open device: %s", soundio_strerror(err));
        release_output_stream(ostream);
        soundio_device_unref(m_out_device);
        m_out_device = nullptr;
        return nullptr;
    }

    if (ostream->layout_error){
        log_message("unable to set channel layout: %s", soundio_strerror(ostream->layout_error));
        release_output_stream(ostream);
        soundio_device_unref(m_out_device);
        m_out_device = nullptr;
        return nullptr;
    }

    return ostream;
}

SoundIoInStream* audioManager::get_in_stream(double latency, int sample_rate, SoundIoFormat format, int device_id)
{
    int err;

    if (m_in_device){
        soundio_device_unref(m_in_device);
        m_in_device = nullptr;
    }

    if (device_id < 0){
        device_id = soundio_default_input_device_index(m_soundio);
    }

    if (device_id < 0) {
        log_message("Input device not found");
        return nullptr;
    }

    m_in_device = soundio_get_input_device(m_soundio, device_id);
    if (!m_in_device) {
        log_message("out of memory");
        return nullptr;
    }

    log_message("Input device: %s", m_in_device->name);

    if (m_in_device->probe_error) {
        log_message("Cannot probe device: %s", soundio_strerror(m_in_device->probe_error));
        return nullptr;
    }

    SoundIoInStream* istream = soundio_instream_create(m_in_device);

    if (!istream) {
        log_message("out of memory");
        return nullptr;
    }

    if (sample_rate < 0){
        std::vector<int> sr = get_input_sample_rates(device_id);
        if (sr.empty()){
            return nullptr;
        }
        sample_rate = sr[0];
    }

    istream->name = m_stream_name.c_str();
    istream->software_latency = latency;
    istream->sample_rate = sample_rate;
    istream->format = format;

    if ((err = soundio_instream_open(istream))) {
        log_message("unable to open device: %s", soundio_strerror(err));
        release_input_stream(istream);
        soundio_device_unref(m_in_device);
        m_in_device = nullptr;
        return nullptr;
    }

    if (istream->layout_error){
        log_message("unable to set channel layout: %s", soundio_strerror(istream->layout_error));
        release_input_stream(istream);
        soundio_device_unref(m_in_device);
        m_in_device = nullptr;
        return nullptr;
    }

    return istream;
}

int audioManager::get_default_output_device_id()
{
    return soundio_default_output_device_index(m_soundio);
}

int audioManager::get_default_input_device_id()
{
    return soundio_default_input_device_index(m_soundio);
}

int audioManager::get_input_device_reverse_map(int mapid)
{
    int count = 0;
    for(auto idx : m_input_map){
        if (idx == mapid){
            return count;
        }
        count++;
    }
    return -1;
}

int audioManager::get_output_device_reverse_map(int mapid)
{
    int count = 0;
    for(auto idx : m_output_map){
        if (idx == mapid){
            return count;
        }
        count++;
    }
    return -1;
}

const std::vector<std::string> audioManager::get_input_sample_rates_str(int devidx)
{
    std::vector<std::string> sample_rates;
    flush();
    if (devidx >= soundio_input_device_count(m_soundio)){
        return sample_rates;
    }
    SoundIoDevice* sndiodev = soundio_get_input_device(m_soundio, devidx);
    if (sndiodev == nullptr){
        return sample_rates;
    }
    for (int i = 0; i < sizeof(g_samplerates)/sizeof(int); ++i){
        if (soundio_device_supports_sample_rate(sndiodev, g_samplerates[i]))
        {
            sample_rates.push_back(std::to_string(g_samplerates[i]));
        }
    }
    soundio_device_unref(sndiodev);
    return sample_rates;
}

const std::vector<std::string> audioManager::get_output_sample_rates_str(int devidx)
{
    std::vector<std::string> sample_rates;
    flush();
    if (devidx >= soundio_output_device_count(m_soundio)){
        return sample_rates;
    }
    SoundIoDevice* sndiodev = soundio_get_output_device(m_soundio, devidx);
    if (sndiodev == nullptr)
    {
        return sample_rates;
    }
    for (int i = 0; i < sizeof(g_samplerates)/sizeof(int); ++i){
        if (soundio_device_supports_sample_rate(sndiodev, g_samplerates[i])){
            sample_rates.push_back(std::to_string(g_samplerates[i]));
        }
    }
    soundio_device_unref(sndiodev);
    return sample_rates;
}

int audioManager::get_sample_rate_by_index(int idx)
{
    return g_samplerates[idx];
}

const std::vector<int> audioManager::get_input_sample_rates(int devidx)
{
    std::vector<int> sample_rates;
    if(devidx >= soundio_input_device_count(m_soundio)){
        return sample_rates;
    }
    SoundIoDevice *sndiodev = soundio_get_input_device(m_soundio, devidx);
    if (sndiodev == nullptr)
    {
        sample_rates.push_back(-1);
        return sample_rates;
    }
    for (int i = 0; i < sizeof(g_samplerates)/sizeof(int); ++i){
        if (soundio_device_supports_sample_rate(sndiodev, g_samplerates[i]))
        {
            sample_rates.push_back(g_samplerates[i]);
        }
    }
    soundio_device_unref(sndiodev);
    return sample_rates;
}

const std::vector<int> audioManager::get_output_sample_rates(int devidx)
{
    std::vector<int> sample_rates;
    if(devidx >= soundio_output_device_count(m_soundio)){
        return sample_rates;
    }
    SoundIoDevice *sndiodev = soundio_get_output_device(m_soundio, devidx);
    if (sndiodev == nullptr)
    {
        sample_rates.push_back(-1);
        return sample_rates;
    }
    for (int i = 0; i < sizeof(g_samplerates)/sizeof(int); ++i){
        if (soundio_device_supports_sample_rate(sndiodev, g_samplerates[i]))
        {
            sample_rates.push_back(g_samplerates[i]);
        }
    }
    soundio_device_unref(sndiodev);
    return sample_rates;
}

ringBuffer* audioManager::get_new_ringbuffer(int capacity)
{
    return new ringBuffer(m_soundio, capacity);
}