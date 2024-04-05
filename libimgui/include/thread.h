#pragma once


//#undef WIN32

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "callback.h"

class ThreadMutex
{
#ifdef WIN32
    CRITICAL_SECTION m_mutex;
#else
    pthread_mutex_t m_mutex;
#endif
public:
    ThreadMutex()
    {
#ifdef WIN32
        InitializeCriticalSection(&m_mutex);
#else
        pthread_mutex_init(&m_mutex, NULL);
#endif
    }
    ~ThreadMutex()
    {
#ifdef WIN32
        DeleteCriticalSection(&m_mutex);
#else
        pthread_mutex_destroy(&m_mutex);
#endif
    }
    bool lock()
    {
#ifdef WIN32
        EnterCriticalSection(&m_mutex);
        return true;
#else
        return pthread_mutex_lock(&m_mutex) == 0;
#endif
    }
    bool unlock()
    {
#ifdef WIN32
        LeaveCriticalSection(&m_mutex);
        return true;
#else
        return pthread_mutex_unlock(&m_mutex) == 0;
#endif
    }
};

class Thread
{
protected:
    UserEvent m_thread_exit_event;
#ifdef WIN32
    DWORD m_thread_id;
    HANDLE m_thread_handle = NULL;
    static DWORD WINAPI run_win32(LPVOID userdata);
#else
    pthread_t m_thread_id;
    void* run_posix();
#endif
    volatile bool m_running;
    volatile bool m_pause;
    bool m_loop;

public:
    Thread(bool loop);
    virtual ~Thread();
    void start();
    bool join();
    void usleep(unsigned long us);

    /*
     * Main thread code
     * If the code needs to loop, set loop=true in the constructor
     */
    virtual void entry() = 0;

    virtual void on_exit_event(Event* sender_object, void* data){}

    void stop()
    {
        m_running = false;
    }

    bool is_running()
    {
        return m_running;
    }

    DECLARE_STATIC_CALLBACK_METHOD(on_exit_event)
};

class ASyncTask : public Thread
{
public:
    ASyncTask() : Thread(false){};
    virtual ~ASyncTask() {}
};

class ASyncLoopTask : public Thread
{
public:
    ASyncLoopTask() : Thread(true){};
    virtual ~ASyncLoopTask() {}
    void pause(bool p)
    {
        m_pause = p;
    }
    bool is_paused()
    {
        return m_pause;
    }
};

