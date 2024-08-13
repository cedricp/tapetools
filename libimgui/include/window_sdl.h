#ifndef IMGUI_WINDOW_SDL
#define IMGUI_WINDOW_SDL

#include <string>
#include <vector>
#include <map>

#include "imgui.h"
#include "implot.h"
#include "callback.h"

class Timer;
class UserEvent;
class SocketNotifier;

struct impl;
struct app_impl;
struct fb_impl;
union SDL_Event;

class Window_SDL;
struct ImFont;

class ChildWidget;

class Widget
{
	bool _is_open = false;
	bool _maximized = false;
	bool _managed = true;
	bool _is_movable = true;
	bool _is_resizable = true;
	bool _titlebar = true;
	bool _modal = false;
	bool _scrollbar = false;
	std::vector<ChildWidget*> _childrens;
protected:
	std::string _name;
	int _posx = 0, _posy = 0;
	int _sizex=400, _sizey=400;
	Window_SDL* _underlying_window = NULL;
public:
	Widget(Window_SDL* win, std::string name, bool managed = true);
	virtual ~Widget();

	Window_SDL* get_underlying_window(){return _underlying_window;}
	void add_child(ChildWidget* child){_childrens.push_back(child);}
	void draw_widget();
	virtual void draw();
	virtual void get_configuration_int(std::map<std::string, int> &){}
	virtual void set_configuration_int(std::string, int) {}

	virtual void get_configuration_float(std::map<std::string, float> &){}
	virtual void set_configuration_float(std::string, float) {}

	virtual void get_configuration_string(std::map<std::string, std::string> &){}
	virtual void set_configuration_string(std::string, std::string) {}

	int width(), height();
	ImVec2 size();

	void update_ui();

	void set_maximized(bool m){_maximized = m;}
	void set_position(int x, int y){_posx = x; _posy = y;}
	void set_size(int x, int y){_sizex= x; _sizey = y;}
	void set_resizable(bool r){_is_resizable = r;}
	void set_movable(bool m){_is_movable= m;}
	void set_titlebar(bool t){_titlebar= t;}
	void set_modal(bool m){_modal = m;}
	void set_scrollbar(bool sb){_scrollbar = sb;}
	friend class Window_SDL;
};

class ChildWidget
{
	bool _is_resizable = true;
protected:
	std::string _name;
	int _sizex=0, _sizey=0;
	Widget* _underlying_widget = NULL;
	bool _sameline;
public:
	ChildWidget(Widget* wid, std::string name, bool sameline, int width = 0, int height = 0);
	virtual ~ChildWidget();

	void draw_widget();

	virtual void draw();

	void set_size(int x, int y){_sizex= x; _sizey = y;}
	friend class Widget;
};

class Window_SDL
{
	UserEvent m_update_event;
	unsigned long m_last_event_time = 0;
public:
	Window_SDL(std::string name, int width = 800, int height=600, bool fullscreen = false);
	virtual ~Window_SDL();

	void show(bool show);

	virtual void draw(bool compute_only=false);
	void add_widget(Widget* widget);
	unsigned int get_windid();
	bool do_event(void* ev);
	void get_window_size(int &w, int &h);
	void set_minimum_window_size(int x, int y);
	void set_maximum_window_size(int x, int y);
	void set_imgui_context();
	bool is_shown();

	void get_configuration_int(std::map<std::string, int>& );
	void set_configuration_int(std::string, int);

	void get_configuration_float(std::map<std::string, float>& );
	void set_configuration_float(std::string, float);
	void get_configuration_string(std::map<std::string, std::string> &cnf);
	void set_configuration_string(std::string s, std::string str);
	unsigned long timestamp();

	void update_ui(){m_update_event.push_delayed(this,0,UserEvent::CODE_UPDATEUI);}

	void set_lazy_mode(bool lazy);
	bool lazy();

	ImFont* load_font_from_memory(const char* data, int memsize, float size);
	void set_last_event_time(){m_last_event_time = timestamp();}
	unsigned long last_event_time(){return m_last_event_time;}

private:
	impl *_impl;
	int _width, _height;
};

class Thread;

class App_SDL
{
	app_impl* _impl;
	void* get_ref_imgui_context();
	std::string m_appname = "unnamed";
	App_SDL();
	~App_SDL();
public:

#ifdef IMP_METHOD
	void destroy();
#endif
	void set_app_name(std::string name){ m_appname = name; }
	ImFont* load_font(std::string fontname, float size);
	ImFont* load_font_from_memory(const char* data, int memsize, float size);
	void register_user_event(UserEvent* ev);
	void unregister_user_event(UserEvent* ev);
	unsigned long timestamp(void);
	std::string get_app_path();
	void add_thread(Thread*);
	Thread* get_thread(std::string name);
	bool abort_thread(std::string name);
	void release_finished_threads();
	void pause_thread(std::string name, bool pause = true);

	void set_str_config(std::string key, std::string val);
	std::string get_str_config(std::string key);

	void run();
	Window_SDL* create_new_window(std::string name, int width=800, int height=600);
	void add_window(Window_SDL* win);
	static App_SDL* get();
	friend class Window_SDL;
};

static auto vector_getter = [](void* vec, int idx, const char** out_text)
{
    auto& vector = *(static_cast<std::vector<std::string>*>(vec));
    if (idx < 0 || idx >= static_cast<int>(vector.size())) { return false; }
    *out_text = vector.at(idx).c_str();
    return true;
};

#endif