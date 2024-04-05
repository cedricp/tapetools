#include "thread.h"
#include <assert.h>
#include <unistd.h>
#include <stdio.h>

	
typedef void * (*THREADFUNCPTR)(void *);

IMPLEMENT_STATIC_CALLBACK_METHOD(on_exit_event, Thread)

#ifdef WIN32
DWORD WINAPI Thread::run_win32(LPVOID userdata)
{
    Thread *thread = (Thread *)userdata;
    HRESULT err = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    assert(err == S_OK);
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
    thread->m_thread_exit_event.push();
    CoUninitialize();
    return 0;
}
#else
void *Thread::run_posix()
{
    while (m_running)
    {
        if (!m_pause)
        {
            entry();
        }
        else
        {
            usleep(10000U);
        }
        if (!m_loop)
            break;
    }
    m_running = false;
    m_thread_exit_event.push();
    pthread_exit(NULL);
    return NULL;
}
#endif

Thread::Thread(bool loop) : m_running(false), m_loop(loop), m_thread_id(0)
{
    CONNECT_CALLBACK((&m_thread_exit_event), on_exit_event)
}

Thread::~Thread()
{
    m_loop = false;
    join();
    m_running = false;
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
    return NULL;
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
