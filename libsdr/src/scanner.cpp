/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012 by Hoernchen <la@tfc-server.de>
 * Copyright (C) 2012 by Kyle Keen <keenerd@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/*
 * rtl_power: general purpose FFT integrator
 * -f low_freq:high_freq:max_bin_size
 * -i seconds
 * outputs CSV
 * time, low, high, step, db, db, db ...
 * db optional?  raw output might be better for noise correction
 * todo:
 *	threading
 *	randomized hopping
 *	noise correction
 *	continuous IIR
 *	general astronomy usefulness
 *	multiple dongles
 *	multiple FFT workers
 *	check edge cropping for off-by-one and rounding errors
 *	1.8MS/s for hiding xtal harmonics
 */

/*
 * Modified to work within the GUI application
 * 2016 - Cedric PAILLE (cedricpaille@gmail.com)
 *
 */

#include "scanner.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <time.h>
#include <utils.h>

#define MAX_TUNES	3000
#define MAXIMUM_RATE			2800000
#define MINIMUM_RATE			1000000
#define DEFAULT_BUF_LENGTH		(1 * 16384)

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define CIC_TABLE_MAX 10

const char* error_codes[] = {
		"No error",
		"Device error",
		"Device connection error",
		"Global error",
		"Memory error"
};

int cic_9_tables[][10] = {
	{0,},
	{9, -156,  -97, 2798, -15489, 61019, -15489, 2798,  -97, -156},
	{9, -128, -568, 5593, -24125, 74126, -24125, 5593, -568, -128},
	{9, -129, -639, 6187, -26281, 77511, -26281, 6187, -639, -129},
	{9, -122, -612, 6082, -26353, 77818, -26353, 6082, -612, -122},
	{9, -120, -602, 6015, -26269, 77757, -26269, 6015, -602, -120},
	{9, -120, -582, 5951, -26128, 77542, -26128, 5951, -582, -120},
	{9, -119, -580, 5931, -26094, 77505, -26094, 5931, -580, -119},
	{9, -119, -578, 5921, -26077, 77484, -26077, 5921, -578, -119},
	{9, -119, -577, 5917, -26067, 77473, -26067, 5917, -577, -119},
	{9, -199, -362, 5303, -25505, 77489, -25505, 5303, -362, -199},
};

inline int16_t FIX_MPY(int16_t a, int16_t b)
/* fixed point multiply and scale */
{
	int c = ((int)a * (int)b) >> 14;
	b = c & 0x01;
	return (c >> 1) + b;
}

inline long real_conj(int16_t real, int16_t imag)
/* real(n * conj(n)) */
{
	return ((long)real*(long)real + (long)imag*(long)imag);
}

SDR_Scanner::SDR_Scanner()
{
	m_boxcar = 1;
	m_comp_fir_size = 0;
	m_peak_hold = 0;
	m_tune_count = 0;
	m_sinewave = NULL;
	m_window_coefs = NULL;
	m_fft_buf = NULL;
}

SDR_Scanner::~SDR_Scanner()
{
	destroy_tunes_memory();

	if (m_rtl_device.device_connected())
	{
		m_rtl_device.close_device();
	}
}

void
SDR_Scanner::destroy_tunes_memory()
{
	for (int i=0; i<m_tune_count; i++) {
		Tuning_state *ts = &m_tunes[i];
		free(ts->avg);
		free(ts->buf8);
	}

	if (m_sinewave)
		free(m_sinewave);
	if (m_fft_buf)
		free(m_fft_buf);
	if (m_window_coefs)
		free(m_window_coefs);

	m_tune_count = 0;
	m_sinewave = nullptr;
	m_fft_buf = nullptr;
	m_window_coefs = nullptr;
}

void
SDR_Scanner::make_sine_table(int size)
{
	int i;
	double d;
	m_log2_nwave = size;
	m_nwave = 1 << m_log2_nwave;
	m_sinewave = (int16_t*)malloc(sizeof(int16_t) * m_nwave*3/4);
	for (i=0; i<m_nwave*3/4; i++)
	{
		d = (double)i * 2.0 * M_PI / m_nwave;
		m_sinewave[i] = (int)round(32767*sin(d));
	}
}

int
SDR_Scanner::fix_fft(int16_t iq[], int m)
/* interleaved iq[], 0 <= n < 2**m, changes in place */
{
	int mr, nn, i, j, l, k, istep, n, shift;
	int16_t qr, qi, tr, ti, wr, wi;
	n = 1 << m;
	if (n > m_nwave)
		{return -1;}
	mr = 0;
	nn = n - 1;
	/* decimation in time - re-order data */
	for (m=1; m<=nn; ++m) {
		l = n;
		do
			{l >>= 1;}
		while (mr+l > nn);
		mr = (mr & (l-1)) + l;
		if (mr <= m)
			{continue;}
		// real = 2*m, imag = 2*m+1
		tr = iq[2*m];
		iq[2*m] = iq[2*mr];
		iq[2*mr] = tr;
		ti = iq[2*m+1];
		iq[2*m+1] = iq[2*mr+1];
		iq[2*mr+1] = ti;
	}
	l = 1;
	k = m_log2_nwave-1;
	while (l < n) {
		shift = 1;
		istep = l << 1;
		for (m=0; m<l; ++m) {
			j = m << k;
			wr =  m_sinewave[j+m_nwave/4];
			wi = -m_sinewave[j];
			if (shift) {
				wr >>= 1; wi >>= 1;}
			for (i=m; i<n; i+=istep) {
				j = i + l;
				tr = FIX_MPY(wr,iq[2*j]) - FIX_MPY(wi,iq[2*j+1]);
				ti = FIX_MPY(wr,iq[2*j+1]) + FIX_MPY(wi,iq[2*j]);
				qr = iq[2*i];
				qi = iq[2*i+1];
				if (shift) {
					qr >>= 1; qi >>= 1;}
				iq[2*j] = qr - tr;
				iq[2*j+1] = qi - ti;
				iq[2*i] = qr + tr;
				iq[2*i+1] = qi + ti;
			}
		}
		--k;
		l = istep;
	}
	return 0;
}

void
SDR_Scanner::rms_power(struct Tuning_state *ts)
/* for bins between 1MHz and 2MHz */
{
	int i, s;
	uint8_t *buf = ts->buf8;
	int buf_len = ts->buf_len;
	long p, t;
	double dc, err;

	p = t = 0L;
	for (i=0; i<buf_len; i++) {
		s = (int)buf[i] - 127;
		t += (long)s;
		p += (long)(s * s);
	}
	/* correct for dc offset in squares */
	dc = (double)t / (double)buf_len;
	err = t * 2 * dc - dc * dc * buf_len;
	p -= (long)round(err);

	if (!m_peak_hold) {
		ts->avg[0] += p;
	} else {
		ts->avg[0] = MAX(ts->avg[0], p);
	}
	ts->samples += 1;
}

int
SDR_Scanner::frequency_range(double crop, int upper, int lower, int max_size)
/* flesh out the tunes[] for scanning */
// do we want the fewest ranges (easy) or the fewest bins (harder)?
{
	int i, j, bw_seen, bw_used, bin_e, buf_len;
	int downsample, downsample_passes;
	double bin_size;
	Tuning_state *ts;

	// Cleanup memory
	destroy_tunes_memory();

	downsample = 1;
	downsample_passes = 0;
	/* evenly sized ranges, as close to MAXIMUM_RATE as possible */
	// todo, replace loop with algebra
	for (i=1; i<1500; i++) {
		bw_seen = (upper - lower) / i;
		bw_used = (int)((double)(bw_seen) / (1.0 - crop));
		if (bw_used > MAXIMUM_RATE)
			continue;
		m_tune_count = i;
		break;
	}
	/* unless small bandwidth */
	if (bw_used < MINIMUM_RATE) {
		m_tune_count = 1;
		downsample = MAXIMUM_RATE / bw_used;
		bw_used = bw_used * downsample;
	}
	if (!m_boxcar && downsample > 1) {
		downsample_passes = (int)log2(downsample);
		downsample = 1 << downsample_passes;
		bw_used = (int)((double)(bw_seen * downsample) / (1.0 - crop));
	}
	/* number of bins is power-of-two, bin size is under limit */
	// todo, replace loop with log2
	for (i=1; i<=21; i++) {
		bin_e = i;
		bin_size = (double)bw_used / (double)((1<<i) * downsample);
		if (bin_size <= (double)max_size) {
			break;}
	}
	/* unless giant bins */
	if (max_size >= MINIMUM_RATE) {
		bw_seen = max_size;
		bw_used = max_size;
		m_tune_count = (upper - lower) / bw_seen;
		bin_e = 0;
		crop = 0;
	}
	if (m_tune_count > MAX_TUNES) {
		return SCANNER_NOK;
	}
	buf_len = 2 * (1<<bin_e) * downsample;
	if (buf_len < DEFAULT_BUF_LENGTH) {
		buf_len = DEFAULT_BUF_LENGTH;
	}
	/* build the array */
	for (i=0; i<m_tune_count; i++) {
		ts = &m_tunes[i];
		ts->freq = lower + i*bw_seen + bw_seen/2;
		ts->rate = bw_used;
		ts->bin_e = bin_e;
		ts->samples = 0;
		ts->crop = crop;
		ts->downsample = downsample;
		ts->downsample_passes = downsample_passes;
		ts->avg = (long*)malloc((1<<bin_e) * sizeof(long));
		if (!ts->avg) {
			return SCANNER_MEMORY_ERROR;
		}
		for (j=0; j<(1<<bin_e); j++) {
			ts->avg[j] = 0L;
		}
		ts->buf8 = (uint8_t*)malloc(buf_len * sizeof(uint8_t));
		if (!ts->buf8) {
			return SCANNER_MEMORY_ERROR;
		}
		ts->buf_len = buf_len;
	}

	m_scan_info.buffer_size_bytes 	=  buf_len;
	m_scan_info.buffer_size_ms 		= 1000 * 0.5 * (float)buf_len / (float)bw_used;
	m_scan_info.cropping_percent	= crop*100;
	m_scan_info.dongle_bw_hz		= bw_used;
	m_scan_info.downsampling		= downsample;
	m_scan_info.fft_bin_size_hz		= bin_size;
	m_scan_info.logged_fft_bins		= (int)((double)(m_tune_count * (1<<bin_e)) * (1.0-crop));
	m_scan_info.num_frequency_hops	= m_tune_count;
	m_scan_info.total_fft_bins		= m_tune_count * (1<<bin_e);

	/* report */
	/*
	printf( "Number of frequency hops: %i\n", m_tune_count);
	printf( "Dongle bandwidth: %iHz\n", bw_used);
	printf( "Downsampling by: %ix\n", downsample);
	printf( "Cropping by: %0.2f%%\n", crop*100);
	printf( "Total FFT bins: %i\n", m_tune_count * (1<<bin_e));
	printf( "Logged FFT bins: %i\n", \
	  (int)((double)(m_tune_count * (1<<bin_e)) * (1.0-crop)));
	printf( "FFT bin size: %0.2fHz\n", bin_size);
	printf( "Buffer size: %i bytes (%0.2fms)\n", buf_len, 1000 * 0.5 * (float)buf_len / (float)bw_used);
	*/
	return SCANNER_OK;

}

void
SDR_Scanner::fifth_order(int16_t *data, int length)
/* for half of interleaved data */
{
	int i;
	int a, b, c, d, e, f;
	a = data[0];
	b = data[2];
	c = data[4];
	d = data[6];
	e = data[8];
	f = data[10];
	/* a downsample should improve resolution, so don't fully shift */
	/* ease in instead of being stateful */
	data[0] = ((a+b)*10 + (c+d)*5 + d + f) >> 4;
	data[2] = ((b+c)*10 + (a+d)*5 + e + f) >> 4;
	data[4] = (a + (b+e)*5 + (c+d)*10 + f) >> 4;
	for (i=12; i<length; i+=4) {
		a = c;
		b = d;
		c = e;
		d = f;
		e = data[i-2];
		f = data[i];
		data[i/2] = (a + (b+e)*5 + (c+d)*10 + f) >> 4;
	}
}

void
SDR_Scanner::remove_dc(int16_t *data, int length)
/* works on interleaved data */
{
	int i;
	int16_t ave;
	long sum = 0L;
	for (i=0; i < length; i+=2) {
		sum += data[i];
	}
	ave = (int16_t)(sum / (long)(length));
	if (ave == 0) {
		return;}
	for (i=0; i < length; i+=2) {
		data[i] -= ave;
	}
}

void
SDR_Scanner::generic_fir(int16_t *data, int length, int *fir)
/* Okay, not at all generic.  Assumes length 9, fix that eventually. */
{
	int d, temp, sum;
	int hist[9] = {0,};
	/* cheat on the beginning, let it go unfiltered */
	for (d=0; d<18; d+=2) {
		hist[d/2] = data[d];
	}
	for (d=18; d<length; d+=2) {
		temp = data[d];
		sum = 0;
		sum += (hist[0] + hist[8]) * fir[1];
		sum += (hist[1] + hist[7]) * fir[2];
		sum += (hist[2] + hist[6]) * fir[3];
		sum += (hist[3] + hist[5]) * fir[4];
		sum +=            hist[4]  * fir[5];
		data[d] = (int16_t)(sum >> 15) ;
		hist[0] = hist[1];
		hist[1] = hist[2];
		hist[2] = hist[3];
		hist[3] = hist[4];
		hist[4] = hist[5];
		hist[5] = hist[6];
		hist[6] = hist[7];
		hist[7] = hist[8];
		hist[8] = temp;
	}
}

void
SDR_Scanner::downsample_iq(int16_t *data, int length)
{
	fifth_order(data, length);
	fifth_order(data+1, length-1);
}

int
SDR_Scanner::scan()
{
	if (m_settings_dirty)
	{
		init();
	}
	if (m_scan_results.size() != m_tune_count)
	{
		m_scan_results.resize(m_tune_count);
	}
	int i, j, j2, f, n_read, offset, bin_e, bin_len, buf_len, ds, ds_p;
	int32_t w;
	struct Tuning_state *ts;
	bin_e = m_tunes[0].bin_e;
	bin_len = 1 << bin_e;
	buf_len = m_tunes[0].buf_len;
	for (i=0; i<m_tune_count; i++) {
		if (m_settings_dirty)
			return SCANNER_NOK;

		ts = &m_tunes[i];

		f = m_rtl_device.get_center_frequency();
		if (f < 1)
			fprintf(stderr, "Warning: RTL cannot set center frequency.\n");
		if (f != ts->freq) {
			int retune_status = m_rtl_device.retune(ts->freq);
			if (retune_status == RTL_BAD_RETUNE){
				fprintf(stderr, "Warning: bad retune.\n");
				return SCANNER_NOK;
			}
			if (retune_status == RTL_CONNECTION_ERROR){
				fprintf(stderr, "Warning: RTL dongle connection problem.\n");
				return SCANNER_NOK;
				break;
			}
		}

		int read_status = m_rtl_device.read_sync(ts->buf8, buf_len, &n_read);
		if (RTL_OK != read_status){
			if (read_status == RTL_DROPPED_SAMPLES) {
				fprintf(stderr, "Warning: dropped samples.\n");
			}
			if (read_status == RTL_CONNECTION_ERROR)
				break;
		}
		/* rms */
		if (bin_len == 1) {
			rms_power(ts);
			continue;
		}
		/* prep for fft */
		for (j=0; j<buf_len; j++) {
			m_fft_buf[j] = (int16_t)ts->buf8[j] - 127;
		}
		ds = ts->downsample;
		ds_p = ts->downsample_passes;
		if (m_boxcar && ds > 1) {
			j=2, j2=0;
			while (j < buf_len) {
				m_fft_buf[j2]   += m_fft_buf[j];
				m_fft_buf[j2+1] += m_fft_buf[j+1];
				m_fft_buf[j] = 0;
				m_fft_buf[j+1] = 0;
				j += 2;
				if (j % (ds*2) == 0) {
					j2 += 2;}
			}
		} else if (ds_p) {  /* recursive */
			for (j=0; j < ds_p; j++) {
				downsample_iq(m_fft_buf, buf_len >> j);
			}
			/* droop compensation */
			if (m_comp_fir_size == 9 && ds_p <= CIC_TABLE_MAX) {
				generic_fir(m_fft_buf, buf_len >> j, cic_9_tables[ds_p]);
				generic_fir(m_fft_buf+1, (buf_len >> j)-1, cic_9_tables[ds_p]);
			}
		}
		remove_dc(m_fft_buf, buf_len / ds);
		remove_dc(m_fft_buf+1, (buf_len / ds) - 1);
		/* window function and fft */
		for (offset=0; offset<(buf_len/ds); offset+=(2*bin_len)) {
			// todo, let rect skip this
			for (j=0; j<bin_len; j++) {
				w =  (int32_t)m_fft_buf[offset+j*2];
				w *= (int32_t)(m_window_coefs[j]);
				m_fft_buf[offset+j*2]   = (int16_t)w;
				w =  (int32_t)m_fft_buf[offset+j*2+1];
				w *= (int32_t)(m_window_coefs[j]);
				m_fft_buf[offset+j*2+1] = (int16_t)w;
			}
			fix_fft(m_fft_buf+offset, bin_e);
			if (!m_peak_hold) {
				for (j=0; j<bin_len; j++) {
					ts->avg[j] += real_conj(m_fft_buf[offset+j*2], m_fft_buf[offset+j*2+1]);
				}
			} else {
				for (j=0; j<bin_len; j++) {
					ts->avg[j] = MAX(real_conj(m_fft_buf[offset+j*2], m_fft_buf[offset+j*2+1]), ts->avg[j]);
				}
			}
			ts->samples += ds;
		}
		Scan_result& current_result = m_scan_results[i];
		compute_fft(current_result, ts);
		current_result.buffer_x.resize(current_result.buffer.size());
		for (int i = 0; i < current_result.buffer.size(); ++i)
		{
			current_result.buffer_x[i] = (current_result.freq_start  + (i * current_result.freq_step)) / 1000000.0;
		}
	}
	return SCANNER_OK;
}

std::string
SDR_Scanner::get_error(int s)
{
	switch(s){
	case SCANNER_MEMORY_ERROR:
		return "Memory allocation failed";
	case SCANNER_DEVICE_ERROR:
		return "SDR device error";
	case SCANNER_DEVICE_CONNECTION:
		return "SDR device connection error";
	case SCANNER_NOK:
		return "SDR compute error";
	default:
		return "Unknown error";
	}
	return "";
}

void SDR_Scanner::set_gain(int gain)
{
	m_rtl_device.set_gain(gain);
}

void SDR_Scanner::set_auto_gain()
{
	m_rtl_device.set_gain(RTL_GAIN_AUTO);
}

int
SDR_Scanner::init()
{
	destroy_tunes_memory();

	double (*window_fn)(int, int);
	// Setup scanner structure

	switch (m_settings.window_type){
	case WINDOW_TYPE_HAMMING:
		window_fn = hamming_fft_window;
		break;
	case WINDOW_TYPE_BLACKMAN:
		window_fn = blackman_fft_window;
		break;
	case WINDOW_TYPE_BACKMAN_HARRIS:
		window_fn = blackman_harris_fft_window;
		break;
	case WINDOW_TYPE_HANN_POISSON:
		window_fn = hann_poisson_fft_window;
		break;
	case WINDOW_TYPE_YOUSSEF:
		window_fn = youssef_fft_window;
		break;
	case WINDOW_TYPE_BARTLETT:
		window_fn = bartlett_fft_window;
		break;
	case WINDOW_TYPE_RECTANGLE:
	default:
		window_fn = rectangle_fft_window;
		break;
	}

	compute_fft_window_corrections(window_fn);

	int status;

	status = frequency_range(m_settings.crop, m_settings.upper_freq, m_settings.lower_freq, m_settings.step_freq);
	if (status != SCANNER_OK){
		std::cerr << "frequency_range error : " << get_error(status) << std::endl;
	}

	if (m_rtl_device.device_connected()){
		m_rtl_device.close_device();
	}

	if (!m_rtl_device.device_connected()){
		status = m_rtl_device.open_device(m_settings.rtl_dev_index);
		if(status != RTL_OK)
			return SCANNER_DEVICE_CONNECTION;
	}
	if (m_settings.direct_sampling){
		status = m_rtl_device.set_direct_sampling(RTL_DIRECT_SAMPLING_MODE_I);
		if(status != RTL_OK)
			return SCANNER_DEVICE_ERROR;
	}

	if (m_settings.offset_tuning){
		status = m_rtl_device.set_offet_tuning_on();
		if(status != RTL_OK)
			return SCANNER_DEVICE_ERROR;
	}

	if (m_settings.gain == -10000){
		status = m_rtl_device.set_auto_gain();
		if(status != RTL_OK)
			return SCANNER_DEVICE_ERROR;
	} else {
		status = m_rtl_device.set_gain(m_settings.gain * 10);
		if(status != RTL_OK)
			return SCANNER_DEVICE_ERROR;
	}

	if(m_settings.ppm_correction != 0){
		status = m_rtl_device.set_ppm(m_settings.ppm_correction);
		if(status != RTL_OK)
			return SCANNER_DEVICE_ERROR;
	}

	status = m_rtl_device.reset_buffer();
	if(status != RTL_OK)
		return SCANNER_DEVICE_ERROR;

	status = m_rtl_device.set_sample_rate(m_tunes[0].rate);
	if(status != RTL_OK)
		return SCANNER_DEVICE_ERROR;

	make_sine_table(m_tunes[0].bin_e);

	m_fft_buf = (int16_t*)malloc(m_tunes[0].buf_len * sizeof(int16_t));
	int length = 1 << m_tunes[0].bin_e;
	m_window_coefs = (int*)malloc(length * sizeof(int));

	// Compute window coeffs
	for (int i=0; i < length; i++) {
		m_window_coefs[i] = (int)(256*window_fn(i, length));
	}

	m_settings_dirty = false;

	return SCANNER_OK;
}

void 
SDR_Scanner::compute_fft_window_corrections(double (*window_fn)(int, int), int num_samples)
{
	double sum = 0;
	double rms = 0;
	double inv_num_samples = 1. / num_samples;
	for (int i = 0; i < num_samples; i++)
	{
		double val = window_fn(i, num_samples);
		sum += val;
		rms += val*val;
	}

	// Normalization
	m_window_amplitude_correction = 1.0 / (sum * inv_num_samples);
	m_window_energy_correction = 1.0 / sqrt(rms * inv_num_samples);
}

void
SDR_Scanner::compute_fft(Scan_result& res, Tuning_state* ts)
{
	int i, len, ds, i1, i2, bw2, bin_count;
	long tmp;
	double dbm;
	len = 1 << ts->bin_e;
	ds = ts->downsample;
	/* fix FFT stuff quirks */
	if (ts->bin_e > 0) {
		/* nuke DC component (not effective for all windows) */
		ts->avg[0] = ts->avg[1];
		/* FFT is translated by 180 degrees */
		for (i=0; i<len/2; i++) {
			tmp = ts->avg[i];
			ts->avg[i] = ts->avg[i+len/2];
			ts->avg[i+len/2] = tmp;
		}
	}
	/* Hz low, Hz high, Hz step, samples, dbm, dbm, ... */
	bin_count = (int)((double)len * (1.0 - ts->crop));
	bw2 = (int)(((double)ts->rate * (double)bin_count) / (len * 2 * ds));

	res.freq_start = ts->freq - bw2;
	res.freq_stop  = ts->freq + bw2;
	res.freq_step  = (float)ts->rate / (double)(len*ds);
	res.num_samples = ts->samples;

	// something seems off with the dbm math
	i1 = 0 + (int)((double)len * ts->crop * 0.5);
	i2 = (len-1) - (int)((double)len * ts->crop * 0.5);
	res.buffer.resize(i2 - i1 + 2);
	int count = 0;
	for (i=i1; i<=i2; i++) {
		dbm  = (double)ts->avg[i];
		dbm /= (double)ts->rate;
		dbm /= (double)ts->samples;
		dbm  = 10 * log10(dbm * m_window_amplitude_correction);
		res.buffer[count++] = dbm;
	}
	dbm = (double)ts->avg[i2] / ((double)ts->rate * (double)ts->samples);
	if (ts->bin_e == 0) {
		dbm = ((double)ts->avg[0] / ((double)ts->rate * (double)ts->samples));}
	dbm  = 10 * log10(dbm * m_window_amplitude_correction);
	res.buffer[count++] = dbm;
	for (i=0; i<len; i++) {
		ts->avg[i] = 0L;
	}
	ts->samples = 0;
}


