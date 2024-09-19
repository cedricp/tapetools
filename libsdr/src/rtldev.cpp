/*
 * (c) 2016 Cedric PAILLE
 * Simple Spectrum Analyzer for RTL Dongle
 *
 */

#include "rtl-sdr.h"
#include "rtldev.h"
#include <unistd.h>
#include <stdio.h>
#include <libusb.h>

struct impl{
	rtlsdr_dev_t *device;
};

#define BUFFER_DUMP	(1<<12)

Rtl_dev::Rtl_dev()
{
	m_device_id = RTL_CONNECTION_ERROR;
	m_impl = new impl;
	m_impl->device = NULL;
}

Rtl_dev::~Rtl_dev()
{
	if (m_device_id >= 0){

	}
	delete m_impl;
}

int
Rtl_dev::get_device_count()
{
	return rtlsdr_get_device_count();
}

int
Rtl_dev::open_device(int dev_idx)
{
	int r = rtlsdr_open(&m_impl->device, (uint32_t)dev_idx);
	if (r < 0){
		m_device_id = RTL_CONNECTION_ERROR;
		m_impl->device = NULL;
		return RTL_CONNECTION_ERROR;
	}
	m_device_id = r;
	return RTL_OK;
}

void
Rtl_dev::close_device()
{
	if (m_device_id >= 0 && m_impl->device)
		rtlsdr_close(m_impl->device);

	m_device_id = -1;
	m_impl->device = NULL;
}

std::vector<int>
Rtl_dev::get_tuner_gains()
{
	std::vector<int> gains;
	if (m_device_id >= 0 && m_impl->device){
		int *g = NULL;
		int count = rtlsdr_get_tuner_gains(m_impl->device, g);
		if (g != NULL){
			for (int i = 0; i < count; ++i){
				gains.push_back(g[i]);
			}
		}
	}
	return gains;
}

int
Rtl_dev::set_sample_rate(int rate)
{
	if (m_device_id >= 0 && m_impl->device){
		int status = rtlsdr_set_sample_rate(m_impl->device, (uint32_t)rate);
		if (status == 0)
			return RTL_OK;
		return RTL_NOK;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::retune(int freq)
{
	if (m_device_id >= 0 && m_impl->device){
		uint8_t dump[BUFFER_DUMP];
		int n_read;
		rtlsdr_set_center_freq(m_impl->device, (uint32_t)freq);
		/* wait for settling and flush buffer */
		usleep(5000);
		rtlsdr_read_sync(m_impl->device, &dump, BUFFER_DUMP, &n_read);
		if (n_read != BUFFER_DUMP) {
			return RTL_BAD_RETUNE;
		}
		return RTL_OK;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::get_center_frequency()
{
	if (m_device_id >= 0 && m_impl->device){
		int f = (int)rtlsdr_get_center_freq(m_impl->device);
		if (f == 0)
			return RTL_NOK;
		return f;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::read_sync(void *buf, int len, int *n_read)
{
	if (m_device_id >= 0 && m_impl->device){
		rtlsdr_read_sync(m_impl->device, buf, len, n_read);
		if (len != *n_read)
			return RTL_DROPPED_SAMPLES;
		return RTL_OK;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::set_direct_sampling(rtl_sampling_mode mode)
{
	if (m_device_id >= 0 && m_impl->device){
		int r = rtlsdr_set_direct_sampling(m_impl->device, (int)mode);
		if (r!=0){
			return RTL_NOK;
		}
		return RTL_OK;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::set_offet_tuning_on()
{
	if (m_device_id >= 0 && m_impl->device){
		int r = rtlsdr_set_offset_tuning(m_impl->device, 1);
		if (r!=0){
			return RTL_NOK;
		}
		return RTL_OK;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::set_offet_tuning_off()
{
	if (m_device_id >= 0 && m_impl->device){
		int r = rtlsdr_set_offset_tuning(m_impl->device, 0);
		if (r!=0){
			return RTL_NOK;
		}
		return RTL_OK;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::set_auto_gain()
{
	if (m_device_id >= 0 && m_impl->device){
		int r = rtlsdr_set_tuner_gain_mode(m_impl->device, 0);
		if (r!=0){
			return RTL_NOK;
		}
		return RTL_OK;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::set_gain(int gain)
{
	if (m_device_id >= 0 && m_impl->device){
		int r = rtlsdr_set_tuner_gain_mode(m_impl->device, 1);
		if (r!=0){
			return RTL_NOK;
		}
		r = rtlsdr_set_tuner_gain(m_impl->device, gain);
		if (r!=0){
			return RTL_NOK;
		}
		return RTL_OK;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::set_ppm(int ppm_error)
{
	if (m_device_id >= 0 && m_impl->device){
		int r = rtlsdr_set_freq_correction(m_impl->device, ppm_error);
		if (r!=0){
			return RTL_NOK;
		}
		return RTL_OK;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::reset_buffer()
{
	if (m_device_id >= 0 && m_impl->device){
		int r = rtlsdr_reset_buffer(m_impl->device);
		if (r!=0){
			return RTL_NOK;
		}
		return RTL_OK;
	}
	return RTL_CONNECTION_ERROR;
}

int
Rtl_dev::device_connected()
{
	if (m_device_id >= 0 && m_impl->device){
		return 1;
	}
	return 0;
}

std::string
Rtl_dev::get_tuner_type()
{
	rtlsdr_tuner tuner = rtlsdr_get_tuner_type(m_impl->device);
	switch(tuner){
	case RTLSDR_TUNER_E4000:
		return "E4000";
	case RTLSDR_TUNER_FC0012:
		return "FC0012";
	case RTLSDR_TUNER_FC0013:
		return "FC0013";
	case RTLSDR_TUNER_FC2580:
		return "FC2580";
	case RTLSDR_TUNER_R820T:
		return "R820T";
	case RTLSDR_TUNER_R828D:
		return "R828D";
	case RTLSDR_TUNER_UNKNOWN:
	default:
		return "Unknown";
	}
	return "";
}

std::string
Rtl_dev::get_name()
{
	if (m_device_id >= 0 && m_impl->device){
		return rtlsdr_get_device_name(m_device_id);
	}
	return "not_connected";
}
