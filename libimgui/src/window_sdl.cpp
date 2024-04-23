#define IMP_METHOD
#include "window_sdl.h"

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <SDL.h>
#include <SDL_opengl.h>

#include <time.h>
#include <sys/time.h>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <map>

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
};

struct app_impl
{
	std::vector<Window_SDL*> _windows;
	std::vector<UserEvent*> m_user_events;
	std::vector<Timer*> m_timers;
	ImGuiContext* _refimguicontext = 0L;
	std::map< std::string, std::string > _str_configs;
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

static App_SDL* _APP_INSTANCE_ = 0L;

Window_SDL::Window_SDL(std::string name, int width, int height, bool fullscreen) : _width(width), _height(height)
{
	_impl = new impl;
	_impl->_name = name;
	show(true);
}

Window_SDL::~Window_SDL()
{
	for (auto win: _impl->widgets){
		delete win;
	}
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

		glEnable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback( GLMessageCallback, 0 );

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();

		// Setup Platform/Renderer backends
		const char* glsl_version = "#version 130";
		ImFont* font = NULL;
		ImGuiContext *ref_ctx = (ImGuiContext*)App_SDL::get()->get_ref_imgui_context();
		ImFontAtlas* atlas = NULL;
		atlas = ImGui::GetIO().Fonts;

		_impl->_imguicontext = ImGui::CreateContext(atlas);
		ImGui::SetCurrentContext(_impl->_imguicontext);
		ImGui_ImplSDL2_InitForOpenGL(_impl->_window, _impl->_gl_context);
		ImGui_ImplOpenGL3_Init(glsl_version);

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();
		//ImGui::StyleColorsClassic();
		_impl->_is_shown = true;
	}
	if (!show && _impl->_is_shown){
		ImGui::SetCurrentContext(_impl->_imguicontext);
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

void Window_SDL::draw(bool compute_only)
{
	if (!_impl->_is_shown)
		return;
	set_imgui_context();
	SDL_GL_MakeCurrent(_impl->_window, _impl->_gl_context);
	//ImGui_ImplSDL2_Set_Window(_impl->_window);
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
	if (event->window.windowID != get_windid())
		return false;
	set_imgui_context();
	bool evok = ImGui_ImplSDL2_ProcessEvent(event);
	if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE && event->window.windowID == get_windid()){
		show(false);
		evok = true;
	}
	return evok;
}

UserEvent::UserEvent()
{
	m_event_idx = SDL_RegisterEvents(1);
	if (m_event_idx != -1)
		App_SDL::get()->register_user_event(this);
	else
		fprintf(stderr, "Cannot register user event\n");
}

UserEvent::~UserEvent()
{
	if (m_event_idx != -1)
		App_SDL::get()->unregister_user_event(this);
}

void UserEvent::push(int code, void* data1, void* data2)
{
	if (m_event_idx != -1){
		SDL_Event event;
		SDL_memset(&event, 0, sizeof(event));
		event.type = m_event_idx;
		event.user.code = code;
		event.user.data1 = data1;
		event.user.data2 = data2;
		SDL_PushEvent(&event);
	} else {
		fprintf(stderr, "Cannot push user event\n");
	}
}


void
UserEvent::on_callback(void* data1, void* data2)
{
	if (m_callback){
		m_userdata1 = data1;
		m_userdata2 = data2;
		m_callback(this, (void*)this, m_callback_data);
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
	ImVec2 winsize = ImGui::GetContentRegionAvail();
	return winsize.x;
}

int
Widget::height()
{
	ImVec2 winsize = ImGui::GetContentRegionAvail();
	return winsize.y;
}

ImVec2
Widget::size()
{
	return ImGui::GetContentRegionAvail();
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0)
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

	atexit(_atexit_);
}


App_SDL::~App_SDL() {
	for (auto window: _impl->_windows){
		if (window) delete window;
	}

	//ImGui::SetCurrentContext(_impl->_refimguicontext);

    // Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext(_implotcontext);
	ImGui::DestroyContext(_impl->_refimguicontext);

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
	std::vector<UserEvent*>::iterator it = std::find(_impl->m_user_events.begin(), _impl->m_user_events.end(), ev);
	if (it != _impl->m_user_events.end())
		return;
	_impl->m_user_events.push_back(ev);
}

void App_SDL::unregister_user_event(UserEvent* ev) {
	std::vector<UserEvent*>::iterator it = std::find(_impl->m_user_events.begin(),_impl->m_user_events.end(), ev);
	if (it == _impl->m_user_events.end())
		return;
	_impl->m_user_events.erase(it);
}

void App_SDL::register_timer(Timer* t)
{
	std::vector<Timer*>::iterator it = std::find(_impl->m_timers.begin(), _impl->m_timers.end(), t);
	if (it != _impl->m_timers.end())
		return;
	_impl->m_timers.push_back(t);
}

void App_SDL::unregister_timer(Timer* t)
{
	std::vector<Timer*>::iterator it = std::find(_impl->m_timers.begin(), _impl->m_timers.end(), t);
	if (it == _impl->m_timers.end())
		return;
	_impl->m_timers.erase(it);
}

ImFont* App_SDL::load_font(std::string fontname, float size)
{
	assert(&ImGui::GetIO());
	ImVector<ImWchar> ranges;
	ImFontGlyphRangesBuilder builder;
	builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
	builder.AddChar(0x221E);                               // Add a specific character
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

bool App_SDL::handle_timer_events()
{
	bool event = false;
	std::vector<Timer*>::iterator it_timer = _impl->m_timers.begin();
	long timestamp = this->timestamp();
	for (; it_timer < _impl->m_timers.end(); ++it_timer){
		if ((*it_timer)->is_active()){
			if (timestamp - (*it_timer)->get_start_time()  >= (*it_timer)->get_time()){
				(*it_timer)->on_timer_event();
				event = true;
			}
		}
	}
	return event;
}

App_SDL* App_SDL::get()
{
	_implotcontext = ImPlot::CreateContext();

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

void App_SDL::run()
{
    bool done = false;

    for(auto window: _impl->_windows){
		window->draw();
	}

    while (!done)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
			if (event.type == SDL_QUIT){
                goto end;
            }

			if (event.type == SDL_WINDOWEVENT){
				if (event.window.event == SDL_WINDOWEVENT_CLOSE){
					goto end;
				}
			}

        	bool event_flag = false;
        	for(auto window: _impl->_windows){
        		window->do_event(&event);
				//window->draw(true);
        	}

        }
		// Events handler
		handle_timer_events();
		std::vector<UserEvent*>::iterator it = _impl->m_user_events.begin();
		for (; it < _impl->m_user_events.end(); ++it){
			if (event.type == (*it)->get_evt_idx()){
				(*it)->on_callback(event.user.data1, event.user.data2);
			}
		}

		//ImPlot::SetCurrentContext(_implotcontext);
		for(auto window: _impl->_windows){
			window->draw(false);
		}

        usleep(5000);
    }
	end:;
}