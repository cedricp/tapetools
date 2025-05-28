#include "thread.h"
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <window_sdl.h>
#include <sys/time.h>

unsigned long timestamp(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) return 0;
    return (unsigned long)((unsigned long)tv.tv_sec * 1000000 + (unsigned long)tv.tv_usec);
}

void Chrono::print_elapsed_time(const char* prefix)
{
    unsigned long time = timestamp() - m_time;
    printf("%s [%i us]\n", prefix, time);
}

typedef void* (*THREADFUNCPTR)(void*);

#ifdef WIN32
DWORD WINAPI Thread::run_win32(LPVOID userdata)
{
    Thread *thread = (Thread *)userdata;
    HRESULT err = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    assert(err == S_OK);
    thread->m_started = true;
    while (thread->m_running)
    {
        if (!thread->m_pause)
        {
            thread->entry();
        }
        else
        {
            thread->usleep(10000U);
        }
        if (!thread->m_loop)
            break;
    }
    thread->m_running = false;
    thread->on_finished();
    CoUninitialize();
    return 0;
}

#else
void *Thread::run_posix(void* userdata)
{
    Thread *thread = (Thread *)userdata;
    thread->m_started = true;
    while (thread->m_running)
    {
        if (!thread->m_pause)
        {
            thread->entry();
        }
        else
        {
            thread->usleep(10000U);
        }
        if (!thread->m_loop)
            break;
    }
    thread->m_running = false;
    thread->on_finished();
    pthread_exit(NULL);
    return NULL;
}
#endif

Thread::Thread(std::string name, bool loop, bool managed) : m_running(false), m_loop(loop), m_thread_id(0), m_name(name), m_pause(false)
{
    if (managed) App_SDL::get()->add_thread(this);
}

Thread::~Thread()
{
    m_loop = false;
    stop();
    join();
}

void Thread::start()
{
    if (!m_running)
    {
        m_running = true;
#ifdef WIN32
        m_thread_handle = CreateThread(NULL, 0, run_win32, this, 0, &m_thread_id);
#else
        pthread_create(&m_thread_id, NULL, (THREADFUNCPTR)&Thread::run_posix, this);
#endif
    }
    m_chrono.reset();
}

void Thread::usleep(unsigned long us)
{
    ::usleep(us);
}

bool Thread::join(){
#ifndef WIN32 
    if (m_thread_id){
        void* status = NULL;

        int retcode = pthread_join(m_thread_id, &status);

        m_thread_id = 0;
        return retcode != 0;
    }
    return false;
#else
    if (m_thread_handle) {
        DWORD err = WaitForSingleObject(m_thread_handle, INFINITE);
        assert(err != WAIT_FAILED);
        BOOL ok = CloseHandle(m_thread_handle);
        assert(ok);
        m_thread_handle = NULL;
        m_thread_id = 0;
        return ok;
    }
    return true;
#endif
}

class Test : public ASyncTask
{
public:
    Test(){}
    virtual ~Test(){}

    void entry() override {
        for(int i=0; i < 10; ++i){
            printf("Entry found ! %i\n", i);
            usleep(500000);
        }
    }
};
