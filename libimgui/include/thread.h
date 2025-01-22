#pragma once


//#undef WIN32

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <string>
#include <functional>

unsigned long timestamp(void);

class Chrono
{
	unsigned long m_time;
public:
	Chrono(){
		reset();
	}

	void reset(){
		m_time = timestamp();
	}

	void print_elapsed_time(const char* prefix);
    unsigned long get_elapsed_time()
    {
        return timestamp() - m_time;
    }
};

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

class ScopedMutex {
    ThreadMutex &m_mutex;
public:
    ScopedMutex(ThreadMutex& mutex) : m_mutex(mutex){
        m_mutex.lock();
    }
    ~ScopedMutex(){
        m_mutex.unlock();
    }
};

class Thread
{
    volatile bool m_running;
protected:
    Chrono m_chrono;
    std::string m_name;
#ifdef WIN32
    DWORD m_thread_id;
    HANDLE m_thread_handle = NULL;
    static DWORD WINAPI run_win32(LPVOID userdata);
#else
    pthread_t m_thread_id;
    static void* run_posix(void* userdata);
#endif
    volatile bool m_pause;
    bool m_loop;
    bool m_started = false;

public:
    Thread(std::string name = "default", bool loop = false, bool managed = true);
    virtual ~Thread();
    void start();
    bool join();
    void usleep(unsigned long us);
    const std::string& name(){return m_name;}
    bool is_started(){return m_started;}

    /*
     * Main thread code
     * If the code needs to loop, set loop=true in the constructor
     */
    virtual void entry() = 0;

    virtual void on_finished(){}

    void stop()
    {
        m_running = false;
    }

    bool is_running()
    {
        return m_running;
    }

    void pause(bool p = true)
    {
        m_pause = p;
    }

    bool is_paused()
    {
        return m_pause;
    }
};

class ASyncTask : public Thread
{
public:
    ASyncTask(std::string name = "defaultasync") : Thread(name, false){};
    virtual ~ASyncTask() {}
};

class ASyncLoopTask : public Thread
{
public:
    ASyncLoopTask(std::string name = "defaultasyncloop") : Thread(name, true){};
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

