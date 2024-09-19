/*
 * (c) 2016 Cedric PAILLE
 * Simple Spectrum Analyzer for RTL Dongle
 *
 */

#ifndef SCANNER_H
#define SCANNER_H
#include <unistd.h>
#include <stdint.h>
#include "rtldev.h"

/* 3000 is enough for 3GHz b/w worst case */
#define MAX_TUNES	3000

enum window_types {
	WINDOW_TYPE_RECTANGLE,
	WINDOW_TYPE_HAMMING,
	WINDOW_TYPE_BLACKMAN,
	WINDOW_TYPE_BACKMAN_HARRIS,
	WINDOW_TYPE_HANN_POISSON,
	WINDOW_TYPE_YOUSSEF,
	WINDOW_TYPE_BARTLETT
};

#define SCANNER_OK		    		 0
#define SCANNER_DEVICE_ERROR 		-1
#define SCANNER_DEVICE_CONNECTION	-2
#define SCANNER_NOK					-3
#define SCANNER_MEMORY_ERROR		-4
#define SCANNER_RESET				-5

#define RTL_GAIN_AUTO -10000

struct Scan_result
{
	int length;
	int freq_start;
	int freq_stop;
	float freq_step;
	int num_samples;
	std::vector<float> buffer;
	std::vector<float> buffer_x;
};

struct scan_info{
	int 	num_frequency_hops;
	int 	dongle_bw_hz;
	int 	downsampling;
	double 	cropping_percent;
	int 	total_fft_bins;
	int 	logged_fft_bins;
	double 	fft_bin_size_hz;
	int 	buffer_size_bytes;
	double 	buffer_size_ms;
};

class Scanner_settings{
public:
	Scanner_settings(){
		lower_freq = 88000000;
		upper_freq = 108000000;
		step_freq = 1000;
		crop = 0.2;
		rtl_dev_index = 0;
		direct_sampling = false;
		offset_tuning = false;
		ppm_correction = 0;
		gain = RTL_GAIN_AUTO;
		window_type = WINDOW_TYPE_HAMMING;
		int rtl_dev_index;
	}
	int lower_freq, upper_freq, step_freq;
	double crop;
	int rtl_dev_index;
	bool direct_sampling;
	bool offset_tuning;
	int gain;
	int ppm_correction;
	window_types window_type;
};

class Scanner{
	struct tuning_state
	/* one per tuning range */
	{
		int 	freq;
		int 	rate;
		int 	bin_e;
		long 	*avg = nullptr;  /* length == 2^bin_e */
		int 	samples;
		int 	downsample;
		int 	downsample_passes;  /* for the recursive filter */
		double 	crop;
		/* having the iq buffer here is wasteful, but will avoid contention */
		uint8_t *buf8;
		int 	buf_len;
	};
	int 		m_nwave, m_log2_nwave;
	int16_t		*m_sinewave;
	int16_t 	*m_fft_buf;
	int 		*m_window_coefs;
	float		m_window_amplitude_correction;
	float		m_window_energy_correction;
	int 		m_boxcar;
	int 		m_comp_fir_size;
	int		 	m_peak_hold;
	int		 	m_tune_count;
	tuning_state m_tunes[MAX_TUNES];
	scan_info	 m_scan_info;
	Rtl_dev		 m_rtl_device;
	bool		m_settings_dirty = true;

	std::vector<Scan_result> m_scan_results;

	void make_sine_table(int size);
	int  fix_fft(int16_t iq[], int m);
	void rms_power(struct tuning_state *ts);
	int  frequency_range(double crop, int upper, int lower, int max_size);
	void fifth_order(int16_t *data, int length);
	void remove_dc(int16_t *data, int length);
	void generic_fir(int16_t *data, int length, int *fir);
	void downsample_iq(int16_t *data, int length);
	void destroy_tunes_memory();
	void compute_fft(Scan_result& res, tuning_state* ts);
	void set_gain(int gain);
	void set_auto_gain();
	Scanner_settings m_settings;
	void compute_fft_window_corrections(double (*window_fn)(int, int), int num_samples = 1000);
public:
	Scanner();
	~Scanner();
	int init();
	int scan();
	const scan_info& get_scan_info(){return m_scan_info;}
	Rtl_dev& get_rtl_device(){return m_rtl_device;}
	std::string get_error(int s);
	const std::vector<Scan_result>& get_scan_result(){return m_scan_results;}

	Scanner_settings& get_settings(){return m_settings;}
	void apply(){m_settings_dirty = true;}
};

#endif
