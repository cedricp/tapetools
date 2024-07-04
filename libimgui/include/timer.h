#ifndef TIMER_H
#define TIMER_H

#include <callback.h>
/*
 * Timer execute callback (one-shot or periodically)
 * when time in milliseconds is reached
 */

class TimerThread;

class Timer : public Event {
	unsigned long m_time;
	bool m_oneshot;
	TimerThread* m_timerthread = nullptr;

public:
	DECLARE_METHODS(on_timer_event);
	Timer(unsigned long time_ms, bool one_shot);
	virtual ~Timer();

	void start();
	void stop();

	bool is_active();
	unsigned long get_time(){return m_time;}
	void set(unsigned long time){m_time = time;}

	void connect_event(event_cb_t ev, void* cb_data){set_callback(ev, cb_data);}

};

#endif