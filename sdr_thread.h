#include <thread.h>
#include <scanner.h>

class SdrThread : public Thread
{
    SDR_Scanner m_scanner;
    bool m_data_available = false;
public:
    SdrThread() : Thread("SdrThread", true, false)
    {
        m_scanner.init();
    }

    ~SdrThread()
    {
        stop();
    }

    SDR_Scanner& get_scanner(){return m_scanner;}
    SDR_Scanner::Scanner_settings& get_scanner_settings(){return m_scanner.get_settings();}

    void lock_graph()
    {
        m_scanner.lock_mutex();
    }

    void unlock_graph()
    {
        m_scanner.unlock_mutex();
    }

    void entry() override
    {
        int scanner_ret;
        if (m_scanner.scan() < SCANNER_OK)
        {
            usleep(500000);
            m_scanner.init();
        }
        else
        {
            m_data_available = true;
        }
    }

    bool data_available()
    {
        return m_data_available;
    }

    const std::vector<SDR_Scanner::Scan_result>& get_scan_result()
    {
        m_data_available = false;
        return m_scanner.get_scan_result();
    }
};