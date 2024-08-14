#define IMP_METHOD
#include "window_sdl.h"

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <imgui_internal.h>
#include <GL/glew.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <thread.h>

#include <time.h>
#include <sys/time.h>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <map>
#include <iostream>

#include "timer.h"
//#include "3dlut.h"

static ImPlotContext* _implotcontext = 0L;

struct impl
{
    SDL_WindowFlags _window_flags;
    SDL_Window* _window = NULL;
    SDL_GLContext _gl_context;
	std::vector<Widget*> widgets;
	ImGuiContext* _imguicontext = NULL;
	bool _is_shown = false;
	std::string _name;
	std::string _inifilename;
	bool lazy_mode = true;
};

struct app_impl
{
	std::vector<Window_SDL*> _windows;
	std::vector<UserEvent*> m_user_events;
	ImGuiContext* _refimguicontext = 0L;
	std::map< std::string, std::string > _str_configs;
	std::vector<Thread*> _threadpool;
};


static void
GLMessageCallback( GLenum source,
GLenum type,
GLuint id,
GLenum severity,
GLsizei length,
const GLchar* message,
const void* userParam )
{
	if(type != GL_DEBUG_TYPE_ERROR){
		return;
	}
	printf("GL CALLBACK: 0x%x <%s> type = 0x%x, severity = 0x%x:\n"
	"%s\n",
	source,
	( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
	type, severity,
	message );

	if(type == GL_DEBUG_TYPE_ERROR)

	{
		//__asm__("int3");
	}
}

static void *UISettingsHandler_ReadOpen(ImGuiContext *ctx, ImGuiSettingsHandler *h, const char *line)
{
	if (strcmp("SettingsF", line) == 0){
		return (void*)1;
	}
	if (strcmp("SettingsI", line) == 0){
		return (void*)2;
	}
	if (strcmp("SettingsS", line) == 0){
		return (void*)3;
	}
	return nullptr;
}

static void UISettingsHandler_WriteAll(ImGuiContext *ctx, ImGuiSettingsHandler *handler, ImGuiTextBuffer *buf)
{
	Window_SDL *wsdl = (Window_SDL*)handler->UserData;
	std::map<std::string, int> mapint;
	std::map<std::string, float> mapfloat;
	std::map<std::string, std::string> mapstring;
	wsdl->get_configuration_int(mapint);
	wsdl->get_configuration_float(mapfloat);
	wsdl->get_configuration_string(mapstring);

	if (!mapint.empty()){
		buf->appendf("[%s][SettingsI]\n", handler->TypeName);
		for (std::pair<std::string, int> cnf: mapint){
			buf->appendf("%s=%i\n", cnf.first.c_str(), cnf.second);
		}
	}

	if (!mapfloat.empty()){
		buf->appendf("[%s][SettingsF]\n", handler->TypeName);
		for (std::pair<std::string, float> cnf: mapfloat){
			buf->appendf("%s=%f\n", cnf.first.c_str(), cnf.second);
		}
	}

	if (!mapstring.empty()){
		buf->appendf("[%s][SettingsS]\n", handler->TypeName);
		for (std::pair<std::string, std::string> cnf: mapstring){
			buf->appendf("%s=\"%s\"\n", cnf.first.c_str(), cnf.second.c_str());
		}
	}
}

static void UISettingsHandler_ReadLine(ImGuiContext *, ImGuiSettingsHandler *handler, void *entry, const char *line)
{
	Window_SDL *wsdl = (Window_SDL *)handler->UserData;

	std::map<std::string, int> map;
	int i = 0;
	char buffer[256];
	char inbuffer[256];

	while(!(*line == '=' || *line == 0)){
		buffer[i++] = *line++;
	}
	buffer[i] = 0;
	line++;i = 0;
	while (*line != 0)
	{
		inbuffer[i++] = *line++;
	}
	inbuffer[i] = 0;
	if (entry == (void*)2){
		if (sscanf(inbuffer, "%d", &i) == 1){
			wsdl->set_configuration_int(buffer, i);
		}
	}
	if (entry == (void*)1){
		float f;
		if (sscanf(inbuffer, "%f", &f) == 1){
			wsdl->set_configuration_float(buffer, f);
		}
	}
	if (entry == (void*)3){
		char bufferstr[256];
		if (sscanf(inbuffer, "\"%[^\"]\"", &bufferstr) == 1){
			wsdl->set_configuration_string(buffer, bufferstr);
		}
	}
}

static App_SDL* _APP_INSTANCE_ = 0L;

Window_SDL::Window_SDL(std::string name, int width, int height, bool fullscreen) : _width(width), _height(height), m_update_event("window_sdl_update_ui")
{
	_impl = new impl;
	_impl->_name = name;
	show(true);
}

Window_SDL::~Window_SDL()
{
	show(false);
	for (auto win: _impl->widgets){
		delete win;
	}
}

void Window_SDL::get_configuration_int(std::map<std::string, int> &cnf)
{
	for (auto win : _impl->widgets)
	{
		win->get_configuration_int(cnf);
	}
}

void Window_SDL::set_configuration_int(std::string s, int i)
{
	for (auto win : _impl->widgets)
	{
		win->set_configuration_int(s, i);
	}
}

void Window_SDL::set_lazy_mode(bool lazy)
{
	_impl->lazy_mode = lazy;
}

bool Window_SDL::lazy()
{
	return _impl->lazy_mode;
}

void Window_SDL::get_configuration_float(std::map<std::string, float> &cnf)
{
	for (auto win : _impl->widgets)
	{
		win->get_configuration_float(cnf);
	}
}

void Window_SDL::set_configuration_float(std::string s, float f)
{
	for (auto win : _impl->widgets)
	{
		win->set_configuration_float(s, f);
	}
}

void Window_SDL::get_configuration_string(std::map<std::string, std::string> &cnf)
{
	for (auto win : _impl->widgets)
	{
		win->get_configuration_string(cnf);
	}
}

void Window_SDL::set_configuration_string(std::string s, std::string str)
{
	for (auto win : _impl->widgets)
	{
		win->set_configuration_string(s, str);
	}
}

unsigned long Window_SDL::timestamp()
{
	return App_SDL::get()->timestamp();
}

void Window_SDL::show(bool show)
{
	if (show && !_impl->_is_shown){
		// Setup SDL
		// (Some versions of SDL before <2.0.10 appears to have performance/stalling issues on a minority of Windows systems,
		// depending on whether SDL_INIT_GAMECONTROLLER is enabled or disabled.. updating to latest version of SDL is recommended!)
		_impl->_window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
		_impl->_window = SDL_CreateWindow(_impl->_name.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, _width, _height, _impl->_window_flags);
		SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
		_impl->_gl_context = SDL_GL_CreateContext(_impl->_window);
		SDL_GL_MakeCurrent(_impl->_window, _impl->_gl_context);
		SDL_GL_SetSwapInterval(1); // Enable vsync

	    if (glewInit() != GLEW_OK){
	    	printf("GLEW init failed\n");
	    	exit(0);
	    }
#ifdef DEBUG
		glEnable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback( GLMessageCallback, 0 );
#endif
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();

		// Setup Platform/Renderer backends
		const char* glsl_version = "#version 130";
		// ImFontAtlas* atlas = NULL;
		// atlas = ImGui::GetIO().Fonts;

		ImGuiContext* current_context = ImGui::GetCurrentContext();
		_impl->_imguicontext = ImGui::CreateContext();
		ImGui::SetCurrentContext(_impl->_imguicontext);

		ImGuiSettingsHandler ui_ini_handler;
		ui_ini_handler.UserData = this;
		ui_ini_handler.TypeName = _impl->_name.c_str();
		ui_ini_handler.TypeHash = ImHashStr(_impl->_name.c_str());
		ui_ini_handler.ReadOpenFn = UISettingsHandler_ReadOpen;
		ui_ini_handler.ReadLineFn = UISettingsHandler_ReadLine;
		ui_ini_handler.WriteAllFn = UISettingsHandler_WriteAll;
		ImGui::AddSettingsHandler(&ui_ini_handler);

		ImGui_ImplSDL2_InitForOpenGL(_impl->_window, _impl->_gl_context);
		ImGui_ImplOpenGL3_Init(glsl_version);

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();
		_impl->_is_shown = true;

		_impl->_inifilename = _impl->_name + ".ini";
		ImGui::GetIO().IniFilename = _impl ->_inifilename.c_str();
		if (current_context) ImGui::SetCurrentContext(current_context);
	}
	if (!show && _impl->_is_shown){
		set_imgui_context();
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext(_impl->_imguicontext);
		SDL_GL_DeleteContext(_impl->_gl_context);
		SDL_DestroyWindow(_impl->_window);
	    _impl->_window = NULL;
	    _impl->_imguicontext = NULL;
	    _impl->_is_shown = false;
	}
}

bool Window_SDL::is_shown()
{
	return _impl->_is_shown;
}

unsigned int Window_SDL::get_windid()
{
	return SDL_GetWindowID(_impl->_window);
}

void
Window_SDL::add_widget(Widget* widget)
{
	if (std::find(_impl->widgets.begin(), _impl->widgets.end(), widget) != _impl->widgets.end()){
		return;
	}
	widget->_underlying_window = this;
	_impl->widgets.push_back(widget);
}

void
Window_SDL::get_window_size(int &w, int &h)
{
	SDL_GetWindowSize(_impl->_window, &w, &h);
}

void
Window_SDL::set_minimum_window_size(int x, int y)
{
	SDL_SetWindowMinimumSize(_impl->_window, x, y);
}

void
Window_SDL::set_maximum_window_size(int x, int y)
{
	SDL_SetWindowMaximumSize(_impl->_window, x, y);
}

void
Window_SDL::set_imgui_context()
{
	ImGui::SetCurrentContext(_impl->_imguicontext);;
}

ImFont* Window_SDL::load_font_from_memory(const char* data, int memsize, float size)
{
	set_imgui_context();
	assert(&ImGui::GetIO());
	ImVector<ImWchar> ranges;
	ImFontGlyphRangesBuilder builder;
	builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
	builder.AddChar(0x221E);
	builder.BuildRanges(&ranges); 
	ImFont* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)data, memsize, size, NULL, ranges.Data, false);
	assert(font);
	unsigned char* pixels;
	int width, height;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	// Upload texture to graphics system
	GLuint tex_font;
	glGenTextures(1, &tex_font);
	glBindTexture(GL_TEXTURE_2D, tex_font);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	ImGui::SetCurrentFont(font);

	return font;
}

void Window_SDL::draw(bool compute_only)
{
	if (!_impl->_is_shown)
		return;
	set_imgui_context();
	SDL_GL_MakeCurrent(_impl->_window, _impl->_gl_context);
	static bool show_demo = true;
    ImVec4 clear_color = ImVec4(0.1f, 0.1f, 0.1f, 1.00f);
    ImGuiIO& io = ImGui::GetIO();

	static bool start = true;
	
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (_impl->widgets.empty()){
    	ImGui::ShowDemoWindow(&show_demo);
    }
	for (auto widget: _impl->widgets){
		widget->draw_widget();
	}

    // Rendering
    ImGui::Render();

	if (compute_only){
		return;
	}

    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(0);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(_impl->_window);
}

bool Window_SDL::do_event(void* ev)
{
	if (!_impl->_is_shown)
		return false;
	SDL_Event* event = (SDL_Event*)ev;
	set_imgui_context();
	bool evok = ImGui_ImplSDL2_ProcessEvent(event);
	return evok;
}

UserEvent::UserEvent(std::string name) : Event(name)
{
	m_event_idx = SDL_RegisterEvents(1);
	if (m_event_idx != -1){
		App_SDL::get()->register_user_event(this);
	} else {
		fprintf(stderr, "Cannot register user event\n");
	}
}

UserEvent::~UserEvent()
{
	if (m_event_idx != -1)
		App_SDL::get()->unregister_user_event(this);
}

void UserEvent::push_delayed(void* data1, void* data2, UserCode code)
{
	if (m_event_idx != -1){
		SDL_Event event;
		SDL_memset(&event, 0, sizeof(event));
		event.type = m_event_idx;
		event.user.code = code;
		event.user.data1 = data1;
		event.user.data2 = data2;
		if (!SDL_PushEvent(&event)){
			fprintf(stderr, "UserEvent::push : Cannot push user event [1]\n");
		}
	} else {
		fprintf(stderr, "UserEvent::push : Cannot push user event [2]\n");
	}
}


Widget::Widget(Window_SDL* win, std::string name, bool managed)
{
	_name = name;
	_managed = managed;
	_underlying_window = win;
	win->add_widget(this);
}

Widget::~Widget()
{
	for (auto child: _childrens){
		delete child;
	}
}

void
Widget::draw()
{

}

int
Widget::width()
{
	return ImGui::GetContentRegionAvail().x;
}

int
Widget::height()
{
	return ImGui::GetContentRegionAvail().y;
}

ImVec2
Widget::size()
{
	return ImGui::GetContentRegionAvail();
}

void Widget::update_ui()
{
	get_underlying_window()->update_ui();
}

void Widget::draw_widget()
{
	if (_managed){
		ImGuiWindowFlags flags = 0;
		if (!_maximized){
			ImGui::SetNextWindowPos(ImVec2(_posx, _posy), !_is_movable ? 0 : ImGuiCond_Once);
			ImGui::SetNextWindowSize(ImVec2(_sizex, _sizey), !_is_resizable ? 0 : ImGuiCond_Once);
			if (!_is_movable){
				flags |= ImGuiWindowFlags_NoMove;
			}
			if (!_is_resizable){
				flags |= ImGuiWindowFlags_NoResize;
			}
			if (!_titlebar){
				flags |= ImGuiWindowFlags_NoTitleBar;
			}
			if (_modal){
				flags |= ImGuiWindowFlags_Modal;
			}
			if (!_scrollbar){
				flags |= ImGuiWindowFlags_NoScrollbar;
			}
		} else if(_underlying_window) {
			int w,h;
			_underlying_window->get_window_size(w, h);
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(w, h), 0);
			_sizex = w;
			_sizey = h;
			flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
			flags |= ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;
		}

		if(ImGui::Begin(_name.c_str(), &_is_open, flags)){
			draw();
			for(auto child: _childrens){
				if (child->_sameline){
					ImGui::SameLine();
				}
				child->draw_widget();
			}
		}

		ImGui::End();
	} else {
		draw();
	}
}

ChildWidget::ChildWidget(Widget* wid, std::string name, bool sameline, int width, int height)
{
	_underlying_widget = wid;
	_sameline = sameline;
	_sizex = width;
	_sizey = height;
	_name = name;
	wid->add_child(this);
}

ChildWidget::~ChildWidget()
{

}

void ChildWidget::draw_widget()
{
	ImGui::BeginChild(_name.c_str(), ImVec2(_sizex, _sizey), ImGuiWindowFlags_HorizontalScrollbar);
	draw();
	ImGui::EndChild();
}

void ChildWidget::draw()
{
}

static void _atexit_(){
	if (_APP_INSTANCE_){
		_APP_INSTANCE_->destroy();
	}
}

App_SDL::App_SDL() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        exit(-1);
    }

    // Setup window
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	_impl = new app_impl;
    _impl->_refimguicontext = ImGui::CreateContext();

	_impl->_str_configs["APP_PATH"] = get_app_path();
	_implotcontext = ImPlot::CreateContext();
	
	atexit(_atexit_);
}


App_SDL::~App_SDL() {
	for (auto thread: _impl->_threadpool){
		delete thread;
	}

	ImGui::SetCurrentContext(_impl->_refimguicontext);
    ImPlot::DestroyContext(_implotcontext);
	ImGui::DestroyContext(_impl->_refimguicontext);

	for (auto window : _impl->_windows)
	{
		if (window)
			delete window;
	}

	delete _impl;
    SDL_Quit();
}

void App_SDL::set_str_config(std::string key, std::string val){
	_impl->_str_configs[key] = val;
}

std::string App_SDL::get_str_config(std::string key){
	return _impl->_str_configs[key];
}

void App_SDL::destroy() {
	delete this;
}

Window_SDL* App_SDL::create_new_window(std::string name, int width, int height) {
	Window_SDL* win = new Window_SDL(name, width, height);
	_impl->_windows.push_back(win);
	return win;
}

void App_SDL::add_window(Window_SDL* win) {
	_impl->_windows.push_back(win);
}

void App_SDL::register_user_event(UserEvent* ev) {
	auto it = std::find(_impl->m_user_events.begin(), _impl->m_user_events.end(), ev);
	if (it != _impl->m_user_events.end()){
		return;
	}
	_impl->m_user_events.push_back(ev);
}

void App_SDL::unregister_user_event(UserEvent* ev) {
	auto it = std::find(_impl->m_user_events.begin(),_impl->m_user_events.end(), ev);
	if (it == _impl->m_user_events.end()){
		return;
	}
	_impl->m_user_events.erase(it);
}

ImFont* App_SDL::load_font(std::string fontname, float size)
{
	assert(&ImGui::GetIO());
	ImVector<ImWchar> ranges;
	ImFontGlyphRangesBuilder builder;
	builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
	builder.AddChar(0x221E);
	builder.BuildRanges(&ranges); 
	ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(fontname.c_str(), size, NULL, ranges.Data);
	assert(font);
	unsigned char* pixels;
	int width, height;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	// Upload texture to graphics system
	GLuint tex_font;
	glGenTextures(1, &tex_font);
	glBindTexture(GL_TEXTURE_2D, tex_font);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	// Store our identifier
	//ImGui::GetIO().Fonts->TexID = (void *)(size_t)tex_font;

	return font;
}

ImFont* App_SDL::load_font_from_memory(const char* data, int memsize, float size)
{
	assert(&ImGui::GetIO());
	ImVector<ImWchar> ranges;
	ImFontGlyphRangesBuilder builder;
	builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
	builder.AddChar(0x221E);
	builder.BuildRanges(&ranges); 
	ImFont* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)data, memsize, size, NULL, ranges.Data, false);
	assert(font);
	unsigned char* pixels;
	int width, height;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	// Upload texture to graphics system
	GLuint tex_font;
	glGenTextures(1, &tex_font);
	glBindTexture(GL_TEXTURE_2D, tex_font);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	// Store our identifier
	//ImGui::GetIO().Fonts->TexID = (void *)(size_t)tex_font;

	return font;
}

App_SDL* App_SDL::get()
{
	if (_APP_INSTANCE_ == 0L){
		_APP_INSTANCE_ = new App_SDL;
	}
	return _APP_INSTANCE_;
}

unsigned long
App_SDL::timestamp(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) return 0;
    return (unsigned long)((unsigned long)tv.tv_sec * 1000 + (unsigned long)tv.tv_usec/1000);
}

void* App_SDL::get_ref_imgui_context()
{
	return (void*)_impl->_refimguicontext;
}

std::string App_SDL::get_app_path()
{
	return SDL_GetBasePath();
}

void App_SDL::add_thread(Thread* thread)
{
	for (auto t: _impl->_threadpool){
		if (t->name() == thread->name()){
			std::cout << "App_SDL::add_thread : Cannot insert multiple threads with the same name" << std::endl;
			delete t;
			return;
		}
	}
	_impl->_threadpool.push_back(thread);
}

Thread* App_SDL::get_thread(std::string name)
{
	for (auto thread: _impl->_threadpool){
		if (name == thread->name()){
			return thread;
		}
	}
	return NULL;
}

bool App_SDL::abort_thread(std::string name)
{
	int threadnum = 0;
	for (auto thread: _impl->_threadpool){
		if (thread->name() == name){
			thread->stop();
			thread->join();
			#ifdef DEBUG
			std::cout << "Deleted thread " << thread->name() << std::endl;
			#endif
			delete thread;
			_impl->_threadpool.erase(_impl->_threadpool.begin() + threadnum);
			return true;
		}
		threadnum++;
	}
	return false;
}

void App_SDL::pause_thread(std::string name, bool pause)
{
	for (auto thread: _impl->_threadpool){
		if (thread->name() == name){
			thread->pause(pause);
		}
	}
}

void App_SDL::release_finished_threads()
{
	std::vector<std::string> del_list;
	for (auto thread: _impl->_threadpool){
		if (!thread->is_running()){
			del_list.push_back(thread->name());
		}
	}
	for(auto del: del_list){
		abort_thread(del);
	}
}

void App_SDL::run()
{
    for(auto window: _impl->_windows){
		window->draw();
	}

    bool done = false;
	bool sleep_mode = false;
    while (!done)
    {
        SDL_Event event;
		if (sleep_mode) SDL_WaitEventTimeout(NULL, 500);
		sleep_mode = true;

        while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT){
                goto end;
            }

			if (event.type == SDL_WINDOWEVENT){
				if (event.window.event == SDL_WINDOWEVENT_CLOSE){
					int i = 0, found = -1;
					for (auto window : _impl->_windows){
						if (window->get_windid() == event.window.windowID){
							found = i;
						}
						++i;
					}
					if (found >= 0){
						auto it = _impl->_windows.begin() + found;
						delete (*it);
						_impl->_windows.erase(it);
					}
					if (_impl->_windows.empty()){
						goto end;
					}
				}
			}

			// Events handler
			for (auto user_event: _impl->m_user_events){
				int idx = user_event->get_evt_idx();
				if (event.type == idx){
					if (event.user.code == UserEvent::CODE_UPDATEUI){
						for (auto window : _impl->_windows){
							if (window == event.user.data1){
								window->set_last_event_time();
							}
						}
					} else {
						user_event->execute(event.user.data1, event.user.data2);
					}
				}
			}

        	for(auto window: _impl->_windows){
				bool win_event = (window->get_windid() == event.window.windowID && window->do_event(&event));
				if (win_event){
					window->set_last_event_time();
				}
        	}
        }
		
		App_SDL::get()->release_finished_threads();

		unsigned long current_time = App_SDL::get()->timestamp();

		auto windows = _impl->_windows;
		for(auto window: windows){
			unsigned long event_time = current_time - window->last_event_time();
			bool force_draw = event_time < 150;
			if (!window->lazy() || force_draw){
				window->draw(false);
				sleep_mode = false;
			} 
		}
    }
	end:;
}