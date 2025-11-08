#include "audio_manager.h"

PAaudioManager::PAaudioManager()
{
    m_pa_ok = false;
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return;
    }

    m_pa_ok = true;
    scan_devices();
}

PAaudioManager::~PAaudioManager()
{
    Pa_Terminate();
}

void PAaudioManager::scan_devices()
{
    if (!m_pa_ok) return;

    m_output_devices.clear();
    m_output_map.clear();

    int output_count = Pa_GetDeviceCount();
    for (int i = 0; i < output_count; ++i){
        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(i);
        if (device_info->maxOutputChannels > 0){
            const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(device_info->hostApi);
            std::string api = apiInfo->name;
            std::string device_name = device_info->name + std::string(" [") + api + std::string("]");
            m_output_devices.push_back(device_name);
            m_output_map.push_back(i);
        }
    }

    m_input_devices.clear();
    m_input_map.clear();
    int input_count = Pa_GetDeviceCount();
    for (int i = 0; i < input_count; ++i){
        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(i);
        if (device_info->maxInputChannels > 0){
            const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(device_info->hostApi);
            std::string api = apiInfo->name;
            std::string device_name = device_info->name + std::string(" [") + api + std::string("]");
            m_input_devices.push_back(device_name);
            m_input_map.push_back(i);
        }
    }
}

std::tuple<PaStream*, StreamInfo> PAaudioManager::get_input_stream(int samplerate, int device_idx, float latency, PaSampleFormat format, PaStreamCallback* callback, void* userData)
{
    StreamInfo info;
    if(!m_pa_ok) return std::make_tuple(nullptr, info);

    int err;
    if (device_idx == paNoDevice) {
        std::cerr << "Error: No default input device.\n";
        Pa_Terminate();
        return std::make_tuple(nullptr, info);;
    }
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(device_idx);
    PaStreamParameters inputParameters;
    inputParameters.device = device_idx;
    inputParameters.channelCount = deviceInfo->maxInputChannels;
    inputParameters.sampleFormat = format;
    inputParameters.suggestedLatency = latency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    info.numChannel = inputParameters.channelCount ;
    info.sampleRate = samplerate;
    info.format = format;

    PaStream* stream;
    err = Pa_OpenStream(
        &stream,
        &inputParameters,
        nullptr,  // no output
        samplerate,
        2048,
        paClipOff,  // no clipping
        callback,    // no callback, we’ll use blocking read
        userData
    );

    if (err != paNoError){
        return std::make_tuple(nullptr, info);
    }

    return std::make_tuple(stream, info);
}

std::tuple<PaStream*, StreamInfo> PAaudioManager::get_output_stream(int samplerate, int device_idx, float latency, PaSampleFormat format, PaStreamCallback* callback, void* userData, int num_channel)
{
    StreamInfo info;
    if(!m_pa_ok) return std::make_tuple(nullptr, info);

    int err;
    if (device_idx == paNoDevice) {
        std::cerr << "Error: No default output device.\n";
        Pa_Terminate();
        return std::make_tuple(nullptr, info);
    }
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(device_idx);
    PaStreamParameters outputParameters;
    outputParameters.device = device_idx;
    outputParameters.channelCount = num_channel > 0 ? num_channel : deviceInfo->maxOutputChannels;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = latency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    info.numChannel = outputParameters.channelCount ;
    info.sampleRate = samplerate;
    info.format = format;

    PaStream* stream;
    err = Pa_OpenStream(
        &stream,
        nullptr,  // no input
        &outputParameters,
        samplerate,
        2048,
        paClipOff,  // no clipping
        callback,    // no callback, we’ll use blocking read
        userData
    );

    if (err != paNoError){
        return std::make_tuple(nullptr, info);
    }

    return std::make_tuple(stream, info);;
}

int PAaudioManager::get_default_output_device_id()
{
    return Pa_GetDefaultOutputDevice();
}

int PAaudioManager::get_default_input_device_id()
{
    return Pa_GetDefaultInputDevice();
}

int PAaudioManager::get_input_device_reverse_map(int mapid)
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

int PAaudioManager::get_output_device_reverse_map(int mapid)
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

ringBuffer* PAaudioManager::get_new_ringbuffer(int capacity)
{
    ringBuffer* rb = new ringBuffer(sizeof(float), capacity);
    return rb;
}

const std::vector<int> PAaudioManager::get_input_sample_rates(int devidx)
{
    std::vector<int> samplerates;

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(devidx);
    const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);

    PaStreamParameters inputParams;
    inputParams.device = devidx;
    inputParams.channelCount = deviceInfo->maxInputChannels;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = deviceInfo->defaultLowOutputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    std::vector<double> rates = {8000, 11025, 16000, 22050, 32000,
                      44100, 48000, 88200, 96000, 192000};

    for (double rate : rates) {
        PaError err = Pa_IsFormatSupported(&inputParams, nullptr, rate);
        if (err == paFormatIsSupported)
            samplerates.push_back(rate);
    }
    return samplerates;
}

const std::vector<int> PAaudioManager::get_output_sample_rates(int devidx)
{
    std::vector<int> samplerates;

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(devidx);
    const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);

    PaStreamParameters outputParams;
    outputParams.device = devidx;
    outputParams.channelCount = deviceInfo->maxOutputChannels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = deviceInfo->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;

    std::vector<double> rates = {8000, 11025, 16000, 22050, 32000,
                      44100, 48000, 88200, 96000, 192000};

    for (double rate : rates) {
        PaError err = Pa_IsFormatSupported(nullptr, &outputParams, rate);
        if (err == paFormatIsSupported)
            samplerates.push_back(rate);
    }
    return samplerates;
}

const std::vector<std::string> PAaudioManager::get_input_sample_rates_str(int devidx)
{
    std::vector<std::string> list;
    for (auto rate : get_input_sample_rates(devidx))
    {
        list.push_back(std::to_string(rate));
    }
    return list;
}

const std::vector<std::string> PAaudioManager::get_output_sample_rates_str(int devidx)
{
    std::vector<std::string> list;
    for (auto rate : get_output_sample_rates(devidx))
    {
        list.push_back(std::to_string(rate));
    }
    return list;
}