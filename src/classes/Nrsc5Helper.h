#ifndef NRSC5HELPER_H
#define NRSC5HELPER_H

#include <mutex>
#include <thread>
#include <condition_variable>

extern "C" {
#include <nrsc5.h>
}
#include <ao/ao.h>
#include <bits/stdc++.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>
#include <stdio.h>
#ifdef __MINGW32__
#include <conio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <termios.h>
#endif


class Nrsc5Helper
{
#define AUDIO_BUFFERS 128
#define AUDIO_THRESHOLD 40
#define AUDIO_DATA_LENGTH 8192
public:
	Nrsc5Helper();
	void init();
	void start(float freq, unsigned int program);
	void stop();
	void quit();

	void change_program(int prog);
	void get_track_info(std::string &artist, std::string &song, std::string &album);
	void get_cover_img(uint8_t (&image)[100000], unsigned int &size);
	void get_logo_img(uint8_t (&image)[100000], unsigned int &size);
private:
	typedef struct
	{
		unsigned int byte;
		unsigned int bits;
		uint8_t *buf, *begin;
	} bitwriter_t;
	static inline void bw_init(bitwriter_t *bw, uint8_t *buf)
	{
		bw->byte = 0;
		bw->bits = 0;
		bw->buf = buf;
		bw->begin = buf;
	}
	static inline void bw_add1bit(bitwriter_t *bw, unsigned int bit)
	{
		bw->byte = (bw->byte << 1) | (!!bit);
		if (++bw->bits == 8)
		{
			*bw->buf++ = static_cast<unsigned char>(bw->byte);
			bw->byte = 0;
			bw->bits = 0;
		}
	}
	static inline void bw_addbits(bitwriter_t *bw, unsigned int value, unsigned int bits)
	{
		unsigned int i;
		for (i = 0; i < bits; ++i)
			bw_add1bit(bw, value & (1 << (bits - i - 1)));
	}
	static inline unsigned int bw_flush(bitwriter_t *bw)
	{
		if (bw->bits)
			bw_addbits(bw, 0, 8 - bw->bits);
		return static_cast<unsigned int>(bw->buf - bw->begin);
	}
	typedef struct buffer_t {
		struct buffer_t *next;
		// The samples are signed 16-bit integers, but ao_play requires a char buffer.
		char data[AUDIO_DATA_LENGTH];
	} audio_buffer_t;
	typedef struct {

		std::string station;
		std::string artist;
		std::string song;
		std::string album;
		int cover_port;
		int logo_port;

	} station_t;
	typedef struct{
		int lot = 0;
		int port = 0;
		uint8_t data[100000];
		unsigned int size = 0;
	} lot_t;
	typedef struct {
		float freq;
		float gain;
		int device_index;
		int ppm_error;
		char *input_name;
		char *rtltcp_host;
		ao_device *dev;
		FILE *hdc_file;
		FILE *iq_file = nullptr;
		char *aas_files_path;

		audio_buffer_t *head, *tail, *free;
		//pthread_mutex_t mutex;
		std::mutex mutex;
		//pthread_cond_t cond;
		std::condition_variable cond;

		unsigned int program;
		unsigned int audio_ready;
		unsigned int audio_packets;
		unsigned int audio_bytes;
		int done;

		station_t station[5];
		lot_t lots[10];

	} state_t;

	nrsc5_t *radio_ = nullptr;
	state_t *st_ = nullptr;

	static ao_device *open_ao_live();
	static void cleanup(state_t *st);
	static void change_program(state_t *st, unsigned int program);
	static void reset_audio_buffers(state_t *st);
	static void dump_aas_file(state_t *st, const nrsc5_event_t *evt);
	static void dump_aas_data(state_t *st, const nrsc5_event_t *evt);
	static void dump_lot_info(state_t *st, int station, nrsc5_sig_component_t *sig_component);
	static void push_audio_buffer(state_t *st, unsigned int program, const int16_t *data, size_t count);
	static void write_adts_header(FILE *fp, unsigned int len);
	static void dump_hdc(FILE *fp, const uint8_t *pkt, unsigned int len);
	//static void dump_ber(float cber);
	static void done_signal(state_t *st);
	static void start_signal(state_t *st);
	static void callback(const nrsc5_event_t *evt, void *opaque);
	void init_audio_buffers(state_t *st);
};

#endif // NRSC5HELPER_H
