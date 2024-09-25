#define _USE_MATH_DEFINES
#include "main_widget.h"

class MainWindow : public Window_SDL
{   
    AudioToolWindow* m_audiotool;
public:
    MainWindow() : Window_SDL("TapeTools", 1200, 900)
    {
        size_t font_data_size = _font_blob_end - _font_blob_start;
        load_font_from_memory((const char*)_font_blob_start, font_data_size, 16);
        m_audiotool = new AudioToolWindow(this);
    }

    bool probe_event() override
    {
        if(m_audiotool->check_data_buffer())
        {
            return true;
        }
        return false;
    }

    virtual ~MainWindow()
    {

    }

    void draw(bool c) override
    {
        Window_SDL::draw(c);
    }
};

int main(int argc, char *argv[])
{
    App_SDL *app = App_SDL::get();
    app->set_app_name("TapeTools");
    Window_SDL *window = new MainWindow;

    window->set_minimum_window_size(1400, 1000);

    app->add_window(window);
    app->run();

    return 0;
}