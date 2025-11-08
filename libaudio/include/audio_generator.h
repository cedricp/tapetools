#include <audio_manager.h>
#include "noise.h"
#include "sine_generator.h"

class PAaudioWaveformGenerator{
public:
    enum generatorMode{
        SINE,
        WHITE_NOISE,
        BROWN_NOISE
    };
private:
    double m_seconds_offset = 0;
    double m_pitch;
    double m_volume = 1.0;
    double m_fm_freq = 0.0;
    double m_fm_strength = 1.0;
    int m_mode = SINE;
    PaStream *m_outstream = nullptr;
    StreamInfo m_outstreaminfo;
    SineWave m_sinewave;

    static int generator_callback(const void* input, void* output,
                    unsigned long frameCount,
                    const PaStreamCallbackTimeInfo* timeInfo,
                    PaStreamCallbackFlags statusFlags,
                    void* userData);

public:
    PAaudioWaveformGenerator();
    ~PAaudioWaveformGenerator();

    StreamInfo& get_info(){return m_outstreaminfo;}

    void destroy();
    bool init(PAaudioManager& manager, int device_idx, int samplerate, float latency);
    bool start();
    bool pause(bool pause = true);

    int  get_samplerate();

    void set_pitch(double pitch, double duration = 0.1);
    void set_volume(int db, double duration = 0.1);

    void set_mode(int m){m_mode = m;}
    int& mode(){return m_mode;};
};