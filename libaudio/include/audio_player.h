#include "audio_manager.h"
#include <inttypes.h>

class audioPlayer
{
    audioManager& m_manager;
    SoundIoOutStream *m_outstream = nullptr;
    ringBuffer*m_ringbuffer = nullptr;
    bool m_playing = false;
    int m_numchannels = 0;

    static void write_callback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max);
    static void underflow_callback(SoundIoOutStream *outstream);
    static void error_callback(SoundIoOutStream *outstream, int err);


public:
    audioPlayer(audioManager& manager) : m_manager(manager)
    {
    }
    ~audioPlayer()
    {
        destroy();
    }
    void destroy();
    bool set(int samplerate, float latency, int device_idx, int channels);
    bool add_data(const int16_t data[], int size);
    void pause(bool pause = true);
};