#ifndef CALLBACK_H
#define CALLBACK_H

class Event;

typedef void (*event_cb_t)(Event*, void*);



#define STATIC_METHOD(methname) static_method_##methname

#define STATIC_CALLBACK_METHOD(methname, ClassType) \
	static void STATIC_METHOD(methname)(Event* base, void* class_instance) \
	{ \
		((ClassType *)class_instance)->methname(base); \
	}

#define DECLARE_STATIC_CALLBACK_METHOD(methname) \
	static void STATIC_METHOD(methname)(Event* base, void* class_instance);

#define IMPLEMENT_STATIC_CALLBACK_METHOD(methname, ClassType) \
	void ClassType::STATIC_METHOD(methname)(Event* base, void* class_instance) \
	{ \
		((ClassType *)class_instance)->methname(base); \
	}

#define DECLARE_CALLBACK_METHOD(methname) void methname(Event* sender_object = nullptr);
#define DECLARE_VIRTUAL_CALLBACK_METHOD(methname) virtual void methname(Event* sender_object);
#define DECLARE_METHODS(methname) DECLARE_STATIC_CALLBACK_METHOD(methname) DECLARE_CALLBACK_METHOD(methname)
#define IMPLEMENT_CALLBACK_METHOD(methname, classname) IMPLEMENT_STATIC_CALLBACK_METHOD(methname, classname) void classname::methname(Event* sender_object)

#define CALLBACK_METHOD(methname, ClassType) STATIC_CALLBACK_METHOD(methname, ClassType) void methname(Event* sender_object = nullptr)

/*
 * Callback connection macros
 */
#define CONNECT_CALLBACK(base, methname) (base)->set_callback(STATIC_METHOD(methname), (void*)this);
#define CONNECT_CALLBACK2(base, methname, class_instance) base->set_callback(STATIC_METHOD(methname), (void*)class_instance);
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

	void connect_event(event_cb_t ev, void* cb_data){set_callback(ev, cb_data);}

	void execute(void* data1, void* data2){
		if(m_callback){
			m_userdata1 = data1;
			m_userdata2 = data2;
			m_callback(this, m_callback_data);
		}
	}
};

class UserEvent : public Event
{
	int m_event_idx;
public:
	enum UserCode{
		CODE_STD = 0,
		CODE_UPDATEUI = 1000,
	};
	UserEvent();
	~UserEvent();

	void push_delayed(void* data1 = 0L, void* data2 = 0L, UserCode code = CODE_STD);
	int get_evt_idx(){return m_event_idx;}
};

#endif