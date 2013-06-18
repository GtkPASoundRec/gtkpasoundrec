
#ifndef _SOUNDREC_HEADER_
#define _SOUNDREC_HEADER_

#include <map>
#include <list>
#include <cstdint>
#include <string>

enum rec_state {
	IDLE, RECORDING, PLAYING_BACK
};

enum rec_type {
	INPUT, DEVICE
};

class Recordable {
	public: 
		rec_type type;
		Recordable(rec_type t) : type(t) {}
		
};

class Input : 
	public Recordable {
	public:
		uint32_t sink;
		uint32_t index;
		std::map<std::string,std::string> props;
		Input(uint32_t s, uint32_t i) : Recordable(INPUT), sink(s), index(i) {}
};

class Device : 
	public Recordable {
	public:
		std::string name;
		uint32_t index;
		Device(const char *n, uint32_t i) : Recordable(DEVICE), name(n), index(i) {}
};

typedef enum {
	NOUPDATE, NEW, CHANGE, REMOVE
} update_t;

void soundrec_start_playback(size_t id);
size_t soundrec_start_recording(Recordable *rec);
void soundrec_stop_playback();
void soundrec_stop_recording();

void soundrec_delete_clip(size_t id);
void soundrec_save_clip(char *filename, size_t id);

rec_state soundrec_get_state();
double soundrec_get_progress();
size_t soundrec_get_pcm(size_t id, size_t start, size_t nbytes, char ***data, size_t **size, size_t *nfrag);

void soundrec_init();

void soundrec_set_inputs_cb(void (*cb)(std::list<Input*> &, std::map<uint32_t, update_t> &));
void soundrec_set_sources_cb(void (*cb)(std::list<Device*> &, std::list<Device*> &));
void soundrec_set_pcm_cb(void (*cb)(size_t));

#endif
