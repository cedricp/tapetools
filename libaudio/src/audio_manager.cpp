#include "audio_manager.h"

#ifdef WIN32
#include "pa_win_wasapi.h"
#endif

void log_message(const char* format, ...);

static std::vector<double> test_rates = {8000, 11025, 16000, 22050, 32000,
                      44100, 48000, 88200, 96000, 192000};

PAaudioManager::PAaudioManager()
{
    m_pa_ok = false;
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        log_message( "PortAudio error: %s\n", Pa_GetErrorText(err));
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
    m_input_devices.clear();

    m_output_map.clear();
    m_input_map.clear();

    m_input_samplerate_cache.clear();
    m_output_samplerate_cache.clear();

    for (int i = 0; i < Pa_GetDeviceCount(); ++i){
        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(i);
        if (device_info->maxOutputChannels > 0){
            const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(device_info->hostApi);
            if (m_use_exclusive_mode && apiInfo->type != paWASAPI) continue;
            std::string api = apiInfo->name;
            std::string device_name = std::string("[") + api + std::string("] ") + device_info->name;
            m_output_devices.push_back(device_name);
            m_output_map.push_back(i);
        }
        if (device_info->maxInputChannels > 0){
            const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(device_info->hostApi);
            if (m_use_exclusive_mode && apiInfo->type != paWASAPI) continue;
            std::string api = apiInfo->name;
            std::string device_name = std::string("[") + api + std::string("] ") + device_info->name;
            m_input_devices.push_back(device_name);
            m_input_map.push_back(i);
        }
    }
}

std::tuple<PaStream*, StreamInfo> PAaudioManager::get_input_stream(int samplerate, int device_idx, float latency, PaSampleFormat format, PaStreamCallback* callback, void* userData)
{
    StreamInfo info;
    if(!m_pa_ok) return std::make_tuple(nullptr, info);

    device_idx = input_to_pa(device_idx);

    int err;
    if (device_idx == paNoDevice) {
        log_message("Error: No default input device.");
        return std::make_tuple(nullptr, info);;
    }
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(device_idx);

    if (latency < deviceInfo->defaultLowInputLatency){
        latency = deviceInfo->defaultLowInputLatency;
        log_message("Latency forced to min [%f]", latency);
    }

    PaStreamParameters inputParameters;
    inputParameters.device = device_idx;
    inputParameters.channelCount = deviceInfo->maxInputChannels;
    inputParameters.sampleFormat = format;
    inputParameters.suggestedLatency = latency;
#ifdef WIN32
    PaWasapiStreamInfo wasapiInfo;
    memset(&wasapiInfo, 0, sizeof(wasapiInfo));
    wasapiInfo.size = sizeof(PaWasapiStreamInfo);
    wasapiInfo.hostApiType = paWASAPI;
    wasapiInfo.version = 1;
    wasapiInfo.flags = (paWinWasapiExclusive|paWinWasapiThreadPriority);
    wasapiInfo.threadPriority = eThreadPriorityProAudio;
    const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
    inputParameters.hostApiSpecificStreamInfo = (apiInfo->type == paWASAPI && m_use_exclusive_mode) ? &wasapiInfo : nullptr;
#else
    inputParameters.hostApiSpecificStreamInfo = nullptr;
#endif

    info.numChannel = inputParameters.channelCount;
    info.sampleRate = samplerate;
    info.format = format;

    PaStream* stream;
    err = Pa_OpenStream(
        &stream,
        &inputParameters,
        nullptr,  // no output
        samplerate,
        0,//(unsigned long)round(latency * samplerate),
        paClipOff,  // no clipping
        callback,
        userData);

    if (err != paNoError){
        //log_message("Cannot initialize input with default channel number, trying 1...");
        // Try with one channel....
        inputParameters.channelCount = 1;
        info.numChannel = 1;
        err = Pa_OpenStream(
            &stream,
            &inputParameters,
            nullptr,  // no output
            samplerate,
            0, //(unsigned long)round(latency * samplerate),
            paClipOff,  // no clipping
            callback,
            userData);
    }

    if (err != paNoError){
        log_message("Error opening input device : %s", Pa_GetErrorText(err));
        return std::make_tuple(nullptr, info);
    }

    return std::make_tuple(stream, info);
}

std::tuple<PaStream*, StreamInfo> PAaudioManager::get_output_stream(int samplerate, int device_idx, float latency, PaSampleFormat format, PaStreamCallback* callback, void* userData, int num_channel)
{
    StreamInfo info;
    if(!m_pa_ok) return std::make_tuple(nullptr, info);
    
    device_idx = output_to_pa(device_idx);

    int err;
    if (device_idx == paNoDevice) {
        log_message("Error: No default output device.");
        return std::make_tuple(nullptr, info);
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(device_idx);

    if (latency < deviceInfo->defaultLowInputLatency){
        latency = deviceInfo->defaultLowInputLatency;
        log_message("Latency forced to min [%f]", latency);
    }

    PaStreamParameters outputParameters;
    outputParameters.device = device_idx;
    outputParameters.channelCount = num_channel > 0 ? num_channel : deviceInfo->maxOutputChannels;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = latency;
#ifdef WIN32
    PaWasapiStreamInfo wasapiInfo;
    memset(&wasapiInfo, 0, sizeof(wasapiInfo));
    wasapiInfo.size = sizeof(PaWasapiStreamInfo);
    wasapiInfo.hostApiType = paWASAPI;
    wasapiInfo.version = 1;
    wasapiInfo.flags = (paWinWasapiExclusive|paWinWasapiThreadPriority);
    wasapiInfo.threadPriority = eThreadPriorityProAudio;
    const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
    outputParameters.hostApiSpecificStreamInfo = (apiInfo->type == paWASAPI && m_use_exclusive_mode) ? &wasapiInfo : nullptr;
#else
    outputParameters.hostApiSpecificStreamInfo = nullptr;
#endif
    info.numChannel = outputParameters.channelCount ;
    info.sampleRate = samplerate;
    info.format = format;

    PaStream* stream;
    err = Pa_OpenStream(
        &stream,
        nullptr,  // no input
        &outputParameters,
        samplerate,
        0, //(unsigned long)round(latency * samplerate),
        paClipOff,  // no clipping
        callback,    // no callback, we’ll use blocking read
        userData
    );

    if (err != paNoError){
        log_message("Error opening output device : %s", Pa_GetErrorText(err));
        return std::make_tuple(nullptr, info);
    }

    return std::make_tuple(stream, info);;
}

void PAaudioManager::safe_close_stream(PaStream** stream)
{
    if (*stream == nullptr) return;
    Pa_CloseStream(*stream);
    *stream = nullptr;
}

int PAaudioManager::get_default_output_device_id()
{
#ifdef WIN32
    int wasapiIndex = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
    if (wasapiIndex >= 0)
    {
        const PaHostApiInfo* wasapiInfo = Pa_GetHostApiInfo(wasapiIndex);
        return pa_to_output(wasapiInfo->defaultOutputDevice);
    }
#endif
    return pa_to_output(Pa_GetDefaultOutputDevice());
}

int PAaudioManager::get_default_input_device_id()
{
#ifdef WIN32
    int wasapiIndex = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
    if (wasapiIndex >= 0)
    {
        const PaHostApiInfo* wasapiInfo = Pa_GetHostApiInfo(wasapiIndex);
        return pa_to_input(wasapiInfo->defaultInputDevice);
    }
#endif
    return pa_to_input(Pa_GetDefaultInputDevice());
}

int PAaudioManager::get_default_output_device_samplerate()
{
    PaDeviceIndex outidx = Pa_GetDefaultOutputDevice();

    if (outidx == paNoDevice)
    {
        return 0;
    }
    const PaDeviceInfo* inputInfo = Pa_GetDeviceInfo(outidx);
    return inputInfo->defaultSampleRate;
    
}

int PAaudioManager::get_default_input_device_samplerate()
{
    PaDeviceIndex inidx = Pa_GetDefaultOutputDevice();

    if (inidx == paNoDevice)
    {
        return 0;
    }
    const PaDeviceInfo* outputInfo = Pa_GetDeviceInfo(inidx);
    return outputInfo->defaultSampleRate;
}


const std::vector<int> PAaudioManager::get_input_sample_rates(int devidx, bool only_default)
{
    std::vector<int> samplerates;

    devidx = input_to_pa(devidx);

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(devidx);

    if (deviceInfo == nullptr) return samplerates;

    if (m_input_samplerate_cache.find(devidx) != m_input_samplerate_cache.end() && !only_default)
    {
        return m_input_samplerate_cache[devidx];
    }

    PaStreamParameters inputParams;
    inputParams.device = devidx;
    inputParams.channelCount = deviceInfo->maxInputChannels;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = deviceInfo->defaultLowOutputLatency;
#ifdef WIN32
    PaWasapiStreamInfo wasapiInfo;
    memset(&wasapiInfo, 0, sizeof(wasapiInfo));
    wasapiInfo.size = sizeof(PaWasapiStreamInfo);
    wasapiInfo.hostApiType = paWASAPI;
    wasapiInfo.version = 1;
    wasapiInfo.flags = (paWinWasapiExclusive|paWinWasapiThreadPriority);
    wasapiInfo.threadPriority = eThreadPriorityProAudio;
    const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
    inputParams.hostApiSpecificStreamInfo = (apiInfo->type == paWASAPI && m_use_exclusive_mode) ? &wasapiInfo : nullptr;
#else
    inputParams.hostApiSpecificStreamInfo = nullptr;
#endif
    double defaultsamplerate = deviceInfo->defaultSampleRate;

    if (only_default){
        samplerates.push_back(defaultsamplerate);
        return samplerates;
    }

    for (double rate : test_rates) {
        PaError err = Pa_IsFormatSupported(&inputParams, nullptr, rate);
        if (err == paFormatIsSupported) samplerates.push_back(rate);
    }

    if (samplerates.empty())
    {
        inputParams.channelCount = 1;
        for (double rate : test_rates) {
            PaError err = Pa_IsFormatSupported(&inputParams, nullptr, rate);
            if (err == paFormatIsSupported) samplerates.push_back(rate);
        }
    }

    m_input_samplerate_cache[devidx] = samplerates;

    return samplerates;
}

const std::vector<int> PAaudioManager::get_output_sample_rates(int devidx, bool only_default)
{
    std::vector<int> samplerates;

    devidx = output_to_pa(devidx);

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(devidx);

    
    if (deviceInfo == nullptr) return samplerates;

    if (m_output_samplerate_cache.find(devidx) != m_output_samplerate_cache.end() && !only_default)
    {
        return m_output_samplerate_cache[devidx];
    }

    PaStreamParameters outputParams;
    outputParams.device = devidx;
    outputParams.channelCount = deviceInfo->maxOutputChannels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = deviceInfo->defaultLowOutputLatency;
    double defaultsamplerate = deviceInfo->defaultSampleRate;
#ifdef WIN32
    PaWasapiStreamInfo wasapiInfo;
    memset(&wasapiInfo, 0, sizeof(wasapiInfo));
    wasapiInfo.size = sizeof(PaWasapiStreamInfo);
    wasapiInfo.hostApiType = paWASAPI;
    wasapiInfo.version = 1;
    wasapiInfo.flags = (paWinWasapiExclusive|paWinWasapiThreadPriority);
    wasapiInfo.threadPriority = eThreadPriorityProAudio;
    const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
    outputParams.hostApiSpecificStreamInfo = (apiInfo->type == paWASAPI && m_use_exclusive_mode) ? &wasapiInfo : nullptr;
#else
    outputParams.hostApiSpecificStreamInfo = nullptr;
#endif
    if (only_default){
        samplerates.push_back(defaultsamplerate);
        return samplerates;
    }

    for (double rate : test_rates) {
        PaError err = Pa_IsFormatSupported(nullptr, &outputParams, rate);
        if (err == paFormatIsSupported) samplerates.push_back(rate);
    }

    m_output_samplerate_cache[devidx] = samplerates;

    return samplerates;
}

const std::vector<std::string> PAaudioManager::get_input_sample_rates_as_stringlist(int devidx)
{
    std::vector<std::string> list;
    for (auto rate : get_input_sample_rates(devidx))
    {
        list.push_back(std::to_string(rate));
    }
    return list;
}

const std::vector<std::string> PAaudioManager::get_output_sample_rates_as_stringlist(int devidx)
{
    std::vector<std::string> list;
    for (auto rate : get_output_sample_rates(devidx))
    {
        list.push_back(std::to_string(rate));
    }
    return list;
}

int PAaudioManager::get_default_input_samplerate_idx(int dev)
{
    const std::vector<int> sr = get_input_sample_rates(dev);
    int default_idx = get_input_sample_rates(dev, true)[0];

    std::vector<int>::const_iterator it = std::find(sr.begin(), sr.end(), default_idx);
    if (it == sr.end()) return 0;
    int idx = std::distance(sr.begin(), it);
    return idx;
}

int PAaudioManager::get_default_output_samplerate_idx(int dev)
{
    const std::vector<int> sr = get_output_sample_rates(dev);
    int default_idx = get_output_sample_rates(dev, true)[0];

    std::vector<int>::const_iterator it = std::find(sr.begin(), sr.end(), default_idx);
    if (it == sr.end()) return 0;
    int idx = std::distance(sr.begin(), it);
    return idx;
}