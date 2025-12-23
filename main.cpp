#define _USE_MATH_DEFINES
#include "main_widget.h"
#include <cstdarg>

class MainWindow : public Window_SDL
{   
    AudioToolWindow* m_audiotool;
public:
    MainWindow() : Window_SDL("TapeTools", 1800, 1000)
    {
        size_t font_data_size = _font_blob_end - _font_blob_start;
        load_font_from_memory((const char*)_font_blob_start, font_data_size, 14);
        m_audiotool = new AudioToolWindow(this);

        // Laziness not enabled...
        set_lazy_mode(false);
    }

    bool probe_event() override
    {
        return m_audiotool->check_data_buffer() == true;
    }

    virtual ~MainWindow()
    {

    }

    void draw(bool c) override
    {
        set_lazy_mode(!m_audiotool->is_compute_on());
        Window_SDL::draw(c);
    }

    void log_message(std::string msg)
    {
        if (m_audiotool)
        {
            m_audiotool->log_message(msg);
        }
    }
};

MainWindow* g_main_window = nullptr;

void log_message(const char* format, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, 1024, format, args);
    va_end(args);
    
    if (g_main_window)
    {
        g_main_window->log_message(msg);
    }
    else
    {
        printf("%s\n", msg);
    }
}

int main(int argc, char *argv[])
{
    App_SDL *app = App_SDL::get();
    app->set_app_name("TapeTools");
    g_main_window = new MainWindow;

    g_main_window->set_minimum_window_size(1800, 1000);

    app->add_window(g_main_window);
    app->run();

    return 0;
}