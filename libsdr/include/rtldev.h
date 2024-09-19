/*
 * (c) 2016 Cedric PAILLE
 * Simple Spectrum Analyzer for RTL Dongle
 *
 */

#ifndef RTLDEV_H
#define RTLDEV_H

#include <stdint.h>
#include <vector>
#include <string>

struct impl;

#define RTL_OK				 0
#define RTL_CONNECTION_ERROR -1
#define RTL_DROPPED_SAMPLES  -2
#define RTL_BAD_RETUNE       -3
#define RTL_NOK				 -4

enum rtl_sampling_mode {
	RTL_DIRECT_SAMPLING_MODE_OFF=0,
	RTL_DIRECT_SAMPLING_MODE_I,
	RTL_DIRECT_SAMPLING_MODE_Q
};

class Rtl_dev
{
	int m_device_id;
	impl *m_impl;
public:
	Rtl_dev();
	~Rtl_dev();

	int  get_device_count();
	int  open_device(int dev_idx);
	void close_device();
	int  set_sample_rate(int rate);
	int  retune(int freq);
	int  get_center_frequency();
	int  read_sync(void *buf, int len, int *n_read);
	int  set_direct_sampling(rtl_sampling_mode mode);
	int  set_offet_tuning_on();
	int  set_offet_tuning_off();
	int  set_auto_gain();
	int  set_gain(int gain);
	int  set_ppm(int ppm_error);
	int	 reset_buffer();
	int  device_connected();
	std::string get_tuner_type();
	std::string get_name();
	std::vector<int> get_tuner_gains();
};

#endif
