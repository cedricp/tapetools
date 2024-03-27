#include <timer.h>
#include <stdio.h>
#include "window_sdl.h"

Timer::Timer(unsigned long time, bool one_shot) : m_time(time),
							m_running(false),
							m_oneshot(one_shot),
							m_start_time(0)
{
	App_SDL::get()->register_timer(this);
}

Timer::~Timer() {
	App_SDL::get()->unregister_timer(this);
}


void Timer::start()
{
	m_start_time = App_SDL::get()->timestamp();
	m_running = true;
}

void Timer::stop() {
	m_running = false;
}

void Timer::on_timer_event()
{
	if (m_oneshot){
		m_running = false;
	} else {
		start();
	}
	if (m_callback){
		m_callback(this, (void*)NULL, m_callback_data);
	} else {
		fprintf(stderr, "Timer event callback not set\n");
	}
}