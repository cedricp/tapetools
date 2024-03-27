#ifndef IMGUI_WINDOW_SDL
#define IMGUI_WINDOW_SDL

#include <string>
#include <vector>

#include "imgui.h"
#include "implot.h"

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

	int width(), height();
	ImVec2 size();

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

private:
	impl* _impl;
	int _width, _height;
};

class App_SDL
{
	app_impl* _impl;
	bool handle_timer_events();
	void* get_ref_imgui_context();
	App_SDL();
	~App_SDL();
public:

#ifdef IMP_METHOD
	void destroy();
#endif
	ImFont* load_font(std::string fontname, float size);
	void register_user_event(UserEvent* ev);
	void unregister_user_event(UserEvent* ev);
	void register_timer(Timer* t);
	void unregister_timer(Timer* t);
	unsigned long timestamp(void);
	std::string get_app_path();

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