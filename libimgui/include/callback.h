#ifndef CALLBACK_H
#define CALLBACK_H

class Event;

typedef void (*event_cb_t)(Event*, void*, void*);

#define STATIC_CALLBACK_METHOD(methname, ClassType) \
	static void static_method_##methname(Event* base, void* data, void* class_instance) \
	{ \
		((ClassType *)class_instance)->methname(base, data); \
	}

#define DECLARE_STATIC_CALLBACK_METHOD(methname) \
	static void static_method_##methname(Event* base, void* data, void* class_instance);

#define IMPLEMENT_STATIC_CALLBACK_METHOD(methname, ClassType) \
	void ClassType::static_method_##methname(Event* base, void* data, void* class_instance) \
	{ \
		((ClassType *)class_instance)->methname(base, data); \
	}
#define DECLARE_CALLBACK_METHOD(methname) void methname(Event* sender_object, void* data);
#define DECLARE_VIRTUAL_CALLBACK_METHOD(methname) virtual void methname(Event* sender_object, void* data);
#define DECLARE_METHODS(methname) DECLARE_STATIC_CALLBACK_METHOD(methname) DECLARE_CALLBACK_METHOD(methname)

#define IMPLEMENT_CALLBACK_METHOD(methname, classname) IMPLEMENT_STATIC_CALLBACK_METHOD(methname, classname) void classname::methname(Event* sender_object, void* data)

/*
 * Callback method macro
 */
#define CALLBACK_METHOD(methname) void methname(Event* sender_object, void* data)


/*
 * Callback connection macros
 */
#define CONNECT_CALLBACK(base, methname) base->set_callback(static_method_##methname, (void*)this);
#define CONNECT_CALLBACK2(base, methname, class_instance) base->set_callback(static_method_##methname, (void*)class_instance);
#define RESET_CALLBACK(base) base->set_callback(NULL, NULL);

class Event {
protected:
	event_cb_t m_callback;
	void *m_callback_data, *m_userdata1, *m_userdata2;
public:
	Event(){
		m_callback = 0L;
		m_callback_data = m_userdata1 = m_userdata2 = 0L;
	}

	void set_callback(event_cb_t cb, void* callback_data){
		m_callback = cb;
		m_callback_data = callback_data;
	}

	void* get_data1(){
		return m_userdata1;
	}

	void* get_data2(){
		return m_userdata2;
	}
};

class UserEvent : public Event
{
	int m_event_idx;
public:
	UserEvent();
	~UserEvent();

	void push(int code = 0, void* data1 = 0L, void* data2 = 0L);
	void on_callback(void* data1 = 0L, void* data2 = 0L);
	int get_evt_idx(){return m_event_idx;}
};

#endif