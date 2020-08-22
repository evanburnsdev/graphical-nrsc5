
extern "C" {
#include <nrsc5.h>
}

#include "Nrsc5Helper.h"
#include <chrono>

Nrsc5Helper::Nrsc5Helper(){}
ao_device *Nrsc5Helper::open_ao_live()
{
	char lr[] = "L,R"; // To appease the C++11 gods
	static ao_sample_format sample_format = {
		16,
		44100,
		2,
		AO_FMT_NATIVE,
		lr
	};
	return ao_open_live(ao_driver_id("pulse"), &sample_format, nullptr);
	//return ao_open_live(ao_default_driver_id(), &sample_format, nullptr);
}
void Nrsc5Helper::cleanup(state_t *st)
{
	reset_audio_buffers(st);
	while (st->free)
	{
		audio_buffer_t *b = st->free;
		st->free = b->next;
		delete b;
	}
	if (st->hdc_file)
		fclose(st->hdc_file);
	if (st->iq_file)
		fclose(st->iq_file);
	delete st->input_name;
	delete st->aas_files_path;
	if (st->dev)
		ao_close(st->dev);
}
void Nrsc5Helper::change_program(state_t *st, unsigned int program)
{
	std::unique_lock<std::mutex> lock(st->mutex);
	//pthread_mutex_lock(&st->mutex);

	// reset audio buffers
	st->audio_ready = 0;
	if (st->tail)
	{
		st->tail->next = st->free;
		st->free = st->head;
		st->head = st->tail = nullptr;
	}
	// update current program
	st->program = program;

	lock.unlock();
	//pthread_mutex_unlock(&st->mutex);
}
void Nrsc5Helper::reset_audio_buffers(state_t *st)
{
	audio_buffer_t *b;

	// find the end of the head list
	for (b = st->head; b && b->next; b = b->next) { }

	// if the head list is non-empty, prepend to free list
	if (b != nullptr)
	{
		b->next = st->free;
		st->free = st->head;
	}

	st->head = nullptr;
	st->tail = nullptr;
}
void Nrsc5Helper::dump_aas_file(state_t *st, const nrsc5_event_t *evt)
{
#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif
	char fullpath[strlen(st->aas_files_path) + strlen(evt->lot.name) + 16];
	FILE *fp;

	sprintf(fullpath, "%s" PATH_SEPARATOR "%d_%s", st->aas_files_path, evt->lot.lot, evt->lot.name);
	fp = fopen(fullpath, "wb");
	if (fp == nullptr)
	{
		fprintf(stderr, "Failed to open %s (%d)\n", fullpath, errno);
		return;
	}
	fwrite(evt->lot.data, 1, evt->lot.size, fp);
	fclose(fp);
}
void Nrsc5Helper::dump_aas_data(state_t *st, const nrsc5_event_t *evt){
	// See if data has already been written
	bool lot_written = false;
	for(int i = 0; i < 10; i++){
		// Check if data written to port
		if(st->lots[i].port == static_cast<int>(evt->lot.port)){
			// See if image at port changed
			if(st->lots[i].lot != static_cast<int>(evt->lot.lot)){
				std::unique_lock<std::mutex> lock(st->mutex);
				st->lots[i].lot = static_cast<int>(evt->lot.lot);
				std::copy(evt->lot.data, evt->lot.data+evt->lot.size, st->lots[i].data);
				st->lots[i].size = evt->lot.size;
				lock.unlock();
			}
			lot_written = true;
			break;
		}
	}
	// Find empty lot and write data
	if(!lot_written){
		for(int i = 0; i < 10; i++){
			if(st->lots[i].port == 0){
				//fprintf(stderr, "Found empty lot to copy data to.\n");
				std::unique_lock<std::mutex> lock(st->mutex);
				st->lots[i].lot = static_cast<int>(evt->lot.lot);
				st->lots[i].port = evt->lot.port;
				std::copy(evt->lot.data, evt->lot.data+evt->lot.size, st->lots[i].data);
				st->lots[i].size = evt->lot.size;
				lock.unlock();
				//fprintf(stderr, "lot: %d port: %d size: %d\n", st->lots[i].lot, st->lots[i].port, st->lots[i].size);
				break;
			}
		}
	}
}
void Nrsc5Helper::dump_lot_info(state_t *st, int station, nrsc5_sig_component_t *sig_component){
	std::unique_lock<std::mutex> lock(st->mutex);
	if(sig_component->data.mime == NRSC5_MIME_PRIMARY_IMAGE){
		st->station[station].cover_port = sig_component->data.port;
		//fprintf(stderr, "Wrote station lot info: Station ID: %d cover_port: %d\n",
		//		station, st->station[sig_component->id-1].cover_port);
	} else if(sig_component->data.mime == NRSC5_MIME_STATION_LOGO){
		st->station[station].logo_port = sig_component->data.port;
		//fprintf(stderr, "Wrote station lot info: Station ID: %d logo_port: %d\n",
		//		station, st->station[sig_component->id-1].logo_port);
	}
	lock.unlock();
}
void Nrsc5Helper::push_audio_buffer(state_t *st, unsigned int program, const int16_t *data, size_t count)
{
	audio_buffer_t *b;

	std::unique_lock<std::mutex> lock(st->mutex);
	//pthread_mutex_lock(&st->mutex);
	if (program != st->program)
		goto unlock;

	while (st->free == nullptr)
	{
		if(st->cond.wait_for(lock, std::chrono::milliseconds(100)) == std::cv_status::timeout){
			fprintf(stderr, "Audio output timed out, dropping samples\n");
			reset_audio_buffers(st);
		}
	}
	b = st->free;
	st->free = b->next;

	lock.unlock();
	//pthread_mutex_unlock(&st->mutex);

	assert(AUDIO_DATA_LENGTH == count * sizeof(data[0]));
	memcpy(b->data, data, count * sizeof(data[0]));

	lock.lock();
	//pthread_mutex_lock(&st->mutex);

	if (program != st->program)
	{
		b->next = st->free;
		st->free = b;
		goto unlock;
	}

	b->next = nullptr;
	if (st->tail)
		st->tail->next = b;
	else
		st->head = b;
	st->tail = b;

	if (st->audio_ready < AUDIO_THRESHOLD)
		st->audio_ready++;

	st->cond.notify_one();
	//pthread_cond_signal(&st->cond);

unlock:
	lock.unlock();
	//pthread_mutex_unlock(&st->mutex);
}
void Nrsc5Helper::write_adts_header(FILE *fp, unsigned int len)
{
	uint8_t hdr[7];
	bitwriter_t bw;

	bw_init(&bw, hdr);
	bw_addbits(&bw, 0xFFF, 12); // sync word
	bw_addbits(&bw, 0, 1); // MPEG-4
	bw_addbits(&bw, 0, 2); // Layer
	bw_addbits(&bw, 1, 1); // no CRC
	bw_addbits(&bw, 1, 2); // AAC-LC
	bw_addbits(&bw, 7, 4); // 22050 HZ
	bw_addbits(&bw, 0, 1); // private bit
	bw_addbits(&bw, 2, 3); // 2-channel configuration
	bw_addbits(&bw, 0, 1);
	bw_addbits(&bw, 0, 1);
	bw_addbits(&bw, 0, 1);
	bw_addbits(&bw, 0, 1);
	bw_addbits(&bw, len + 7, 13); // frame length
	bw_addbits(&bw, 0x7FF, 11); // buffer fullness (VBR)
	bw_addbits(&bw, 0, 2); // 1 AAC frame per ADTS frame

	fwrite(hdr, 7, 1, fp);
}
void Nrsc5Helper::dump_hdc(FILE *fp, const uint8_t *pkt, unsigned int len)
{
	write_adts_header(fp, len);
	fwrite(pkt, len, 1, fp);
	fflush(fp);
}
/*void Nrsc5Helper::dump_ber(float cber)
{
	static float min = 1, max = 0, sum = 0, count = 0;
	sum += cber;
	count += 1;
	if (cber < min) min = cber;
	if (cber > max) max = cber;
	printf("BER: %f, avg: %f, min: %f, max: %f\n", cber, sum / count, min, max);
}*/
void Nrsc5Helper::done_signal(state_t *st)
{
	std::unique_lock<std::mutex> lock(st->mutex);
	//pthread_mutex_lock(&st->mutex);
	st->done = 1;
	st->cond.notify_one();
	//pthread_cond_signal(&st->cond);
	lock.unlock();
	//pthread_mutex_unlock(&st->mutex);

}
void Nrsc5Helper::start_signal(state_t *st)
{
	std::unique_lock<std::mutex> lock(st->mutex);
	//pthread_mutex_lock(&st->mutex);
	st->done = 0;
	st->cond.notify_one();
	//pthread_cond_signal(&st->cond);
	lock.unlock();
	//pthread_mutex_unlock(&st->mutex);

}
void Nrsc5Helper::callback(const nrsc5_event_t *evt, void *opaque)
{
	state_t *st = static_cast<state_t*>(opaque);
	nrsc5_sig_service_t *sig_service;
	nrsc5_sig_component_t *sig_component;
	nrsc5_sis_asd_t *audio_service;
	nrsc5_sis_dsd_t *data_service;

	switch (evt->event)
	{
	case NRSC5_EVENT_LOST_DEVICE:
		done_signal(st);
		break;
	case NRSC5_EVENT_BER:
		//dump_ber(evt->ber.cber);
		break;
	case NRSC5_EVENT_MER:
		//printf("MER: %.1f dB (lower), %.1f dB (upper)\n", evt->mer.lower, evt->mer.upper);
		break;
	case NRSC5_EVENT_IQ:
		if (st->iq_file)
			fwrite(evt->iq.data, 1, evt->iq.count, st->iq_file);
		break;
	case NRSC5_EVENT_HDC:
		if (evt->hdc.program == st->program)
		{
			if (st->hdc_file)
				dump_hdc(st->hdc_file, evt->hdc.data, static_cast<unsigned int>(evt->hdc.count));

			st->audio_packets++;
			st->audio_bytes += evt->hdc.count * sizeof(evt->hdc.data[0]);
			if (st->audio_packets >= 32) {
				//printf("Audio bit rate: %.1f kbps\n", static_cast<float>(st->audio_bytes) * 8 * 44100 / 2048 / st->audio_packets / 1000);
				st->audio_packets = 0;
				st->audio_bytes = 0;
			}
		}
		break;
	case NRSC5_EVENT_AUDIO:
		push_audio_buffer(st, evt->audio.program, evt->audio.data, evt->audio.count);
		break;
	case NRSC5_EVENT_SYNC:
		//printf("Synchronized\n");
		st->audio_ready = 0;
		break;
	case NRSC5_EVENT_LOST_SYNC:
		//printf("Lost synchronization\n");
		break;
	case NRSC5_EVENT_ID3:
		//if (evt->id3.program == st->program)
		//{
		if (evt->id3.title)
			st->station[evt->id3.program].song = evt->id3.title;
			//printf("Title: %s\n", evt->id3.title);
		if (evt->id3.artist)
			st->station[evt->id3.program].artist = evt->id3.artist;
			//printf("Artist: %s\n", evt->id3.artist);
		if (evt->id3.album)
			st->station[evt->id3.program].album = evt->id3.album;
			//printf("Album: %s\n", evt->id3.album);
		//if (evt->id3.genre)
			//printf("Genre: %s\n", evt->id3.genre);
		//if (evt->id3.ufid.owner)
			//printf("Unique file identifier: %s %s\n", evt->id3.ufid.owner, evt->id3.ufid.id);
		//if (evt->id3.xhdr.param >= 0)
			//st->station[evt->id3.program].lot = static_cast<int>(evt->id3.xhdr.lot);
			//fprintf(stderr,"XHDR: %d %08X %d\n", evt->id3.xhdr.param, evt->id3.xhdr.mime, evt->id3.xhdr.lot);
		//}
		break;
	case NRSC5_EVENT_SIG:
		for (sig_service = evt->sig.services; sig_service != nullptr; sig_service = sig_service->next)
		{
			//fprintf(stderr, "SIG Service: type=%s number=%d name=%s\n",
			//		 sig_service->type == NRSC5_SIG_SERVICE_AUDIO ? "audio" : "data",
			//		 sig_service->number, sig_service->name);

			for (sig_component = sig_service->components; sig_component != nullptr; sig_component = sig_component->next)
			{
				if (sig_component->type == NRSC5_SIG_SERVICE_AUDIO)
				{
					//fprintf(stderr, "  Audio component: id=%d port=%04X type=%d mime=%08X\n", sig_component->id,
					//		 sig_component->audio.port, sig_component->audio.type, sig_component->audio.mime);
				}
				else if (sig_component->type == NRSC5_SIG_SERVICE_DATA)
				{
					dump_lot_info(st, sig_service->number-1, sig_component);
					//fprintf(stderr, "  Data component: id=%d port=%04X service_data_type=%d type=%d mime=%08X\n",
					//		 sig_component->id, sig_component->data.port, sig_component->data.service_data_type,
					//		 sig_component->data.type, sig_component->data.mime);
				}
			}
		}
		break;
	case NRSC5_EVENT_LOT:
		//if (st->aas_files_path)
		//	dump_aas_file(st, evt);
		//fprintf(stderr,"LOT file: port=%04X lot=%d name=%s size=%d mime=%08X\n", evt->lot.port, evt->lot.lot, evt->lot.name, evt->lot.size, evt->lot.mime);
		dump_aas_data(st, evt);

		break;
	case NRSC5_EVENT_SIS:
		if (evt->sis.country_code)
			printf("Country: %s, FCC facility ID: %d\n", evt->sis.country_code, evt->sis.fcc_facility_id);
		if (evt->sis.name)
			printf("Station name: %s\n", evt->sis.name);
		if (evt->sis.slogan)
			printf("Slogan: %s\n", evt->sis.slogan);
		if (evt->sis.message)
			printf("Message: %s\n", evt->sis.message);
		if (evt->sis.alert)
			printf("Alert: %s\n", evt->sis.alert);
		if (!isnan(evt->sis.latitude))
			//printf("Station location: %f, %f, %dm\n", evt->sis.latitude, evt->sis.longitude, evt->sis.altitude);
		for (audio_service = evt->sis.audio_services; audio_service != nullptr; audio_service = audio_service->next)
		{
			const char *name = nullptr;
			nrsc5_program_type_name(audio_service->type, &name);
			printf("Audio program %d: %s, type: %s, sound experience %d\n",
					 audio_service->program,
					 audio_service->access == NRSC5_ACCESS_PUBLIC ? "public" : "restricted",
					 name, audio_service->sound_exp);
		}
		for (data_service = evt->sis.data_services; data_service != nullptr; data_service = data_service->next)
		{
			const char *name = nullptr;
			nrsc5_service_data_type_name(data_service->type, &name);
			printf("Data service: %s, type: %s, MIME type %03x\n",
					 data_service->access == NRSC5_ACCESS_PUBLIC ? "public" : "restricted",
					 name, data_service->mime_type);
		}
		break;
	}
}
void Nrsc5Helper::init_audio_buffers(state_t *st)
{
	st->head = nullptr;
	st->tail = nullptr;
	st->free = nullptr;

	for (int i = 0; i < AUDIO_BUFFERS; ++i)
	{
		audio_buffer_t *b = new audio_buffer_t;
		b->next = st->free;
		st->free = b;
	}

	//pthread_cond_init(&st->cond, NULL);
	//pthread_mutex_init(&st->mutex, NULL);
}
void Nrsc5Helper::init()
{
	st_ = new state_t;
	st_->gain = -1;
	st_->ppm_error = INT_MIN;
#ifdef __MINGW32__
	SetConsoleOutputCP(CP_UTF8);
#endif
	ao_initialize();
	init_audio_buffers(st_);
	st_->dev = open_ao_live();
	if(nrsc5_open(&radio_, st_->device_index) != 0)
	{
		return;
	}

}
void Nrsc5Helper::start(float freq, unsigned int program){
	st_->freq = freq*1e6f;
	st_->program = program;

	// Clear ports if restarted to avoid image mismatches
	st_->station[st_->program].cover_port = 0;
	st_->station[st_->program].logo_port = 0;
	if (st_->ppm_error != INT_MIN && nrsc5_set_freq_correction(radio_, st_->ppm_error) != 0)
	{
		printf("Set frequency correction failed.\n");
		return;
	}
	//fprintf(stderr, "Freq: %f\n", st_->freq);
	if (nrsc5_set_frequency(radio_, st_->freq) != 0)
	{
		printf("Set frequency failed.\n");
		return;
	}
	if (st_->gain >= 0.0f)
		nrsc5_set_gain(radio_, st_->gain);
	nrsc5_set_callback(radio_, callback, st_);
	start_signal(st_);
	nrsc5_start(radio_);

	//nrsc5_log_set_level(3);

	// Used for terminal inputs - not needed
	// will need to create thread for gui inputs.
	// to call change_program, etc
	//pthread_create(&input_thread, NULL, input_main, st);
	while (1)
	{
		audio_buffer_t *b;

		std::unique_lock<std::mutex> lock(st_->mutex);
		//pthread_mutex_lock(&st->mutex);
		while (!st_->done && (st_->head == nullptr || st_->audio_ready < AUDIO_THRESHOLD))
			st_->cond.wait(lock);
			//pthread_cond_wait(&st->cond, &st->mutex);

		// exit once done and no more audio buffers
		if (st_->head == nullptr)
		{
			lock.unlock();
			//pthread_mutex_unlock(&st->mutex);
			break;
		}

		// unlink from head list
		b = st_->head;
		st_->head = b->next;
		if (st_->head == nullptr)
			st_->tail = nullptr;
		lock.unlock();
		//pthread_mutex_unlock(&st->mutex);

		ao_play(st_->dev, b->data, sizeof(b->data));

		lock.lock();
		//fprintf(stderr, "Song: %s\n", st_->song.c_str());
		//pthread_mutex_lock(&st->mutex);
		// add to free list
		b->next = st_->free;
		st_->free = b;
		st_->cond.notify_one();
		//pthread_cond_signal(&st->cond);
		lock.unlock();
		//pthread_mutex_unlock(&st->mutex);
	}
	nrsc5_stop(radio_);
}
void Nrsc5Helper::stop(){
	done_signal(st_);
	change_program(st_, static_cast<unsigned int>(-1));
	fprintf(stderr, "Stopping...\n");
}
void Nrsc5Helper::quit(){
	fprintf(stderr, "Cleaning up before quitting...\n");
	nrsc5_close(radio_);
	cleanup(st_);
	delete st_;
	ao_shutdown();
}
void Nrsc5Helper::change_program(int prog){
	change_program(st_, static_cast<unsigned int>(prog));
}
void Nrsc5Helper::get_track_info(std::string &artist, std::string &song, std::string &album){
	artist = st_->station[st_->program].artist;
	song = st_->station[st_->program].song;
	album = st_->station[st_->program].album;
}
void Nrsc5Helper::get_cover_img(uint8_t (&image)[100000], unsigned int &size){
	//uint8_t cover_image[100000];
	//std::memset(cover_image, 0, 100000*sizeof(uint8_t));
	//fprintf(stderr, "Station Program: %d Port: %d\n", st_->program, st_->station[st_->program].logo_port);

	// No Image
	if (st_->station[st_->program].cover_port == 0){
		size = 99999;
		return;
	}
	for(int i = 0; i < 10; i++){
		if((st_->station[st_->program].cover_port != 0) && (st_->station[st_->program].cover_port == st_->lots[i].port)){
			//fprintf(stderr, "Found matching lot.\n");
			if(memcmp(image, st_->lots[i].data, st_->lots[i].size) != 0){
				fprintf(stderr,"Updated Image\n");
				size = st_->lots[i].size;
				std::copy(st_->lots[i].data, st_->lots[i].data+size, image);
				return;
			} else {
				// Image matches displayed, nothing to do
				size = 0;
				return;
			}
		}
	}
	// No matching Image Found, clear
	size = 99999;
}
void Nrsc5Helper::get_logo_img(uint8_t (&image)[100000], unsigned int &size){
	//uint8_t logo_image[100000];
	//std::memset(logo_image, 0, 100000*sizeof(uint8_t));
	//fprintf(stderr, "Station Program: %d Port: %d\n", st_->program, st_->station[st_->program].logo_port);

	// No Image
	if (st_->station[st_->program].logo_port == 0){
		size = 99999;
		return;
	}
	for(int i = 0; i < 10; i++){
		if((st_->station[st_->program].logo_port != 0) && (st_->station[st_->program].logo_port == st_->lots[i].port)){
			//fprintf(stderr, "Found matching lot.\n");
			if(memcmp(image, st_->lots[i].data, st_->lots[i].size) != 0){
				fprintf(stderr,"Updated Image\n");
				size = st_->lots[i].size;
				std::copy(st_->lots[i].data, st_->lots[i].data+size, image);
				return;
			} else {
				// Image matches displayed, nothing to do
				size = 0;
				return;
			}
		}
	}
	// No matching Image Found, clear
	size = 99999;
}
