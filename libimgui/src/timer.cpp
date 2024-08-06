#include <timer.h>
#include <stdio.h>
#include "window_sdl.h"
#include "thread.h"

class TimerThread : public Thread
{
	unsigned long m_time;
	bool m_oneshot;
public:
	UserEvent timer_event;
	TimerThread(unsigned long time, bool oneshot) : Thread("timer_thread", !oneshot, false), m_time (time) , m_oneshot(oneshot){

	}

	~TimerThread(){
	}

	void entry() override {
		for(int i = 0; i < 10; ++i){
			usleep(m_time * 100);
			if (!is_running()){
				break;
			}
		}
		timer_event.push_delayed((void*)10UL);
	}
};

Timer::Timer(unsigned long time, bool one_shot) : m_time(time), m_oneshot(one_shot)
{
}

Timer::~Timer() {
	if (m_timerthread){
		delete m_timerthread;
	}
}

void Timer::start()
{
	if (m_timerthread != nullptr){
		delete m_timerthread;
	}
	m_timerthread = new TimerThread(m_time, m_oneshot);
	m_timerthread->timer_event.connect_event(STATIC_METHOD(on_timer_event), this);
	m_timerthread->start();
}

void Timer::stop() {
	if (m_timerthread){
		delete m_timerthread;
	}
	m_timerthread = nullptr;
}

IMPLEMENT_CALLBACK_METHOD(on_timer_event, Timer)
{
	if (m_callback){
		m_callback(this, m_callback_data);
	} else {
		fprintf(stderr, "Timer event callback not set\n");
	}
}

bool Timer::is_active()
{
	if (m_timerthread == nullptr) return false;

	return m_timerthread->is_running();
}
