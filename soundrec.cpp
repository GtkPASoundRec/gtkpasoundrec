
#include <map>
#include <list>
#include <string>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <cmath>

#include <sndfile.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include <glib.h>

#include "soundrec.hpp"

extern "C" {
	/* The sample format to use */
	static const pa_sample_spec ss = {
		.format = PA_SAMPLE_S16LE,
		.rate = 44100,
		.channels = 2
	};
}

using namespace std;

#define BLOCK_SIZE (1024*1024)

class Clip {
	private:
		list<char *>::iterator it;
		static size_t num_clips;
	public: 
		list<char *> blocks;
		char *buf;
		size_t capacity;
		size_t rec_size;
		size_t played_size;
		size_t id;
		static map<size_t,Clip*> clip_map;
		Clip() : capacity(0), rec_size(0), played_size(0) {
			this->expand();
			id = num_clips++;
			clip_map[id] = this;
		}
		char* expand() {
			blocks.push_back((char *)malloc(BLOCK_SIZE));
			capacity += BLOCK_SIZE;
			return buf = blocks.back();
		}
		char* next() {
			return buf = *(++it);
		}
		char* rewind() {
			it = blocks.begin();
			return buf = blocks.front();
		}
		~Clip() {
			clip_map.erase(id);
			it = blocks.begin();
			while (it != blocks.end()) {
				free(*it++);
			}
		}
};

map<size_t,Clip*> Clip::clip_map;
size_t Clip::num_clips = 0;

namespace soundrec {
	const char *monitor, *mic;
	list<Device*> sinks;
	list<Device*> sources;
	list<Device*> mics;
	list<Device*> monitors;
	
	bool got_sinks;
	bool got_sources;
	bool got_monitors;
	
	void (*user_inputs_cb)(list<Input*>&, map<uint32_t,update_t>&) = NULL;
	void (*user_sources_cb)(list<Device*>&, list<Device*>&) = NULL;
	void (*user_pcm_cb)(size_t) = NULL;
	
	Clip *cur;
	
	list<Input*> inputs;
	
	/*struct timeval tf, ts;
	bool got_first = false;*/

	map<uint32_t, Device*> monitor_map;
	map<uint32_t, update_t> update_map;
	map<uint32_t, update_t> update_map_frozen;
	bool update_pending = false;

	pa_stream *rs = NULL;
	pa_stream *ps = NULL;
	pa_context *ctx;

	rec_state state = IDLE;
}

template <class T>
void clear_list(list<T> l) {
	typename list<T>::iterator it;
	for (it=l.begin(); it!=l.end(); it++) {
		delete (*it);
	}
	l.clear();
}

using namespace soundrec;

void update_monitors() {
	list<Device*>::iterator si, so;
	string *sink, *source;
	uint32_t sink_idx;

	for (si = sinks.begin(); si != sinks.end(); si++) {
		sink = &((*si)->name);
		for (so = sources.begin(); so != sources.end(); so++) {
			source = &((*so)->name);
			if (strncmp(source->c_str(), sink->c_str(), sink->length()) == 0) {
				sink_idx = (*si)->index;
				
				assert(monitor_map.count(sink_idx) == 0);
				
				monitor_map[sink_idx] = (*so);
				got_monitors = true;
				
				monitor = source->c_str();
				monitors.push_back(*so);
			} else {
				mic = source->c_str();
				mics.push_back(*so);
			}
		}
	}
	assert(got_monitors);
	if (user_sources_cb != NULL) {
		user_sources_cb(monitors, mics);
	}
}

void sink_cb(pa_context *c, const pa_sink_info *l, int eol, void *data) {
	if (eol == 0) {
		sinks.push_back(new Device(l->name, l->index));
	} else {
		got_sinks = true;
		if (got_sources) {
			update_monitors();
		}
	}
}

void source_cb(pa_context *c, const pa_source_info *l, int eol, void *data) {
	if (eol == 0) {
		sources.push_back(new Device(l->name, l->index));
	} else {
		got_sources = true;
		if (got_sinks) {
			update_monitors();
		}
	}
}

void update_dev_names(pa_context *c) {
	pa_operation *op1, *op2;
	
	got_sinks = false;
	got_sources = false;
	got_monitors = false;
	
	monitor_map.clear();
	
	clear_list<Device*>(sources);
	clear_list<Device*>(sinks);
	clear_list<Device*>(mics);
	clear_list<Device*>(monitors);
	
	op1 = pa_context_get_sink_info_list(c, sink_cb, NULL);
	op2 = pa_context_get_source_info_list(c, source_cb, NULL);
	
	pa_operation_unref(op1);
	pa_operation_unref(op2);
}

bool need_to_update(uint32_t idx) {
	list<Input*>::iterator it;
	bool have_input = false;
	bool input_changed = false;
	
	for (it=inputs.begin(); it != inputs.end(); it++) {
		if ((*it)->index == idx) {
			have_input = true;
			break;
		}
	}
	
	if (have_input) {
		if (update_map_frozen.count(idx) == 0) {
			update_map_frozen[idx] = NOUPDATE;
			
		} else {
			assert(update_map_frozen[idx] != NEW);
			assert(update_map_frozen[idx] != REMOVE);
			
			if (update_map_frozen[idx] == CHANGE) {
				input_changed = true;
			}
		}
	} else {
		update_map_frozen[idx] = NEW;
	}
	
	return !have_input || input_changed;
}

void sink_input_cb(pa_context *c, const pa_sink_input_info *l, int eol, void *data) {
	const char *key, *val;
	void *iter = NULL;
	Input *input;
	if (eol == 0) {
		if (need_to_update(l->index)) {
			input = new Input(l->sink, l->index);
			while ((key = pa_proplist_iterate(l->proplist, &iter)) != NULL) {
				val = pa_proplist_gets(l->proplist, key);
				input->props[string(key)] = string(val);
			}
			inputs.push_back(input);
		}
	} else {
		if (user_inputs_cb != NULL) {
			user_inputs_cb(inputs, update_map_frozen);
		}
		update_pending = false;
	}
}

void update_sink_inputs(pa_context *c) {
	pa_operation *op;
	
	update_pending = true;
	op = pa_context_get_sink_input_info_list(c, sink_input_cb, NULL);
	pa_operation_unref(op);
}

gboolean update_cb(void *data) {
	pa_context *c = (pa_context *)data;
	list<Input*>::iterator it;
	Input *inp;
	
	for (it = inputs.begin(); it != inputs.end(); it++) {
		inp = *it;
		if (update_map[inp->index] == REMOVE || update_map[inp->index] == CHANGE) {
			delete inp;
			inputs.erase(it--);
		}
	}
	update_sink_inputs(c);
	update_map_frozen = update_map;
	update_map.clear();
	return FALSE;
}

void queue_update(pa_context *c, uint32_t idx, update_t upd) {
	update_map[idx] = upd;
	if (!update_pending) {
		update_pending = true;
		g_timeout_add(100, update_cb, (void *)c);
	}
}

void subscribe_inputs_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *data) {
	int type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
	switch (type) {
		case PA_SUBSCRIPTION_EVENT_NEW: 
			queue_update(c, idx, NEW);
			break;
		case PA_SUBSCRIPTION_EVENT_CHANGE:
			queue_update(c, idx, CHANGE);
			break;
		case PA_SUBSCRIPTION_EVENT_REMOVE:
			queue_update(c, idx, REMOVE);
			break;
		default:;
	}
}
void connect_cb(pa_context *c, void *) {
	pa_context_state_t state = pa_context_get_state(c);
	pa_operation *op;
	switch (state) {
		case PA_CONTEXT_FAILED:
		case PA_CONTEXT_TERMINATED:
			fprintf(stderr, __FILE__": connection failed\n");
			break;
		case PA_CONTEXT_READY:
			update_sink_inputs(c);
			update_dev_names(c);
			
			pa_context_set_subscribe_callback(c, subscribe_inputs_cb, NULL);
			op = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL);
			
			pa_operation_unref(op);
			break;
		default:;
	}
}

void soundrec_stop_playback() {
	pa_stream_disconnect(ps);
	pa_stream_unref(ps);
	state = IDLE;
}

void soundrec_stop_recording() {
	pa_stream_disconnect(rs);
	pa_stream_unref(rs);
	state = IDLE;
}

void read_cb(pa_stream *s, size_t nbytes, void *data) {
	char *bh;
	size_t ns;
	size_t bytes_top;
	size_t bytes_in_block;
	
	pa_stream_peek(s, (const void **)&data, &nbytes);
	
	bytes_in_block = cur->rec_size - (BLOCK_SIZE*(cur->rec_size/BLOCK_SIZE));

	bh = cur->buf + bytes_in_block;
	bytes_top = BLOCK_SIZE - bytes_in_block;

	ns = cur->rec_size + nbytes;
	
	if (nbytes > bytes_top) {
		memcpy(bh, data, bytes_top);
		nbytes -= bytes_top;
		data += bytes_top;

		if (ns > cur->capacity) {
			bh = cur->expand();
		} else {
			bh = cur->next();
		}
	}
	
	memcpy(bh, data, nbytes);
	cur->rec_size = ns;
	
	if (user_pcm_cb != NULL) {
		user_pcm_cb(nbytes);
	}

	pa_stream_drop(s);
}

void my_free(void *) {}

void write_cb(pa_stream *s, size_t nbytes, void *data) {
	char *bh;
	size_t bytes_left;
	size_t bytes_top;
	size_t bytes_in_block;
	
	if (state != PLAYING_BACK) 
		return;
	
	bytes_in_block = cur->played_size - (BLOCK_SIZE*(cur->played_size/BLOCK_SIZE));

	bh = cur->buf + bytes_in_block;
	bytes_left = cur->rec_size - cur->played_size;
	
	if (nbytes > bytes_left) {
		nbytes = bytes_left;
		bytes_left = 0;
	}

	bytes_top = BLOCK_SIZE - bytes_in_block;

	if (nbytes > bytes_top) {
		pa_stream_write(s, bh, bytes_top, my_free, 0, PA_SEEK_RELATIVE);

		cur->played_size += bytes_top;
		nbytes -= bytes_top;

		bh = cur->next();
	}

	pa_stream_write(s, bh, nbytes, my_free, 0, PA_SEEK_RELATIVE);
	
	cur->played_size += nbytes;

	if (bytes_left == 0) {
		soundrec_stop_playback();
	}
}

size_t soundrec_start_recording(Recordable *rec) {
	Input *inp;
	Device *dev;
	const char *name;
	
	assert(state == IDLE);
	assert(rec != NULL);
	
	state = RECORDING;
	cur = new Clip();
	 
	rs = pa_stream_new(ctx, "Record", &ss, NULL);
	
	pa_stream_set_read_callback(rs, read_cb, NULL);
	
	if (rec->type == INPUT) {
		inp = static_cast<Input*>(rec);
		pa_stream_set_monitor_stream(rs, inp->index);
		
		dev = monitor_map[inp->sink];
	} else {
		dev = static_cast<Device*>(rec);
	}
	name = dev->name.c_str();
	
	pa_stream_connect_record(rs, name, NULL, (pa_stream_flags_t)0);
	
	return cur->id;
}

void soundrec_start_playback(size_t id) {
	assert(state == IDLE);
	
	state = PLAYING_BACK;
	
	cur = Clip::clip_map[id];
	cur->rewind();
	cur->played_size = 0;
	
	ps = pa_stream_new(ctx, "Playback", &ss, NULL);
	
	pa_stream_set_write_callback(ps, write_cb, NULL);
	pa_stream_connect_playback(ps, NULL, NULL, (pa_stream_flags_t)0, NULL, NULL);
}

void soundrec_save_clip(char *filename, size_t id) {
	Clip *clip;
	list<char *>::iterator it;
	int i, nblocks;
	size_t bytes_last_block;
	sf_count_t n;

	// For saving with libsndfile
	SF_INFO sfinfo;
	sfinfo.samplerate = 44100;
	sfinfo.channels = 2;
	sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
	sfinfo.frames = cur->rec_size/4;

	SNDFILE *f = sf_open(filename, SFM_WRITE, &sfinfo);
	if (f == NULL) {
		printf("Save failed: %s\n", sf_strerror(NULL));
	}
	
	clip = Clip::clip_map[id];
	assert(clip != NULL);
	
	it = clip->blocks.begin();
	nblocks = clip->rec_size/BLOCK_SIZE;

	for (i=0; i<nblocks; i++, it++) {
		n = sf_write_raw(f, *it, BLOCK_SIZE);
		if (n < 0) {
			printf("Write failed: %s\n", sf_strerror(NULL));
		}
	}

	bytes_last_block = clip->rec_size - (nblocks*BLOCK_SIZE);
	n = sf_write_raw(f, *it, bytes_last_block);
	
	if (n < 0) {
		printf("Write failed: %s\n", sf_strerror(NULL));
	}

	if (sf_close(f)) {
		printf("Error closing\n");
	}
}

void soundrec_init() {
	pa_glib_mainloop *ml;
	pa_mainloop_api *api;
	
	ml  = pa_glib_mainloop_new(NULL);
	api = pa_glib_mainloop_get_api(ml);
	ctx = pa_context_new(api, "SoundRecorder");
	
	pa_context_set_state_callback(ctx, connect_cb, NULL);
	pa_context_connect(ctx, NULL, (pa_context_flags_t)0, NULL);
}

rec_state soundrec_get_state() {
	return state;
}

double soundrec_get_progress() {
	return ((double)cur->played_size)/cur->rec_size;
}

size_t soundrec_get_pcm(size_t id, size_t start, size_t nbytes, char ***data, size_t **size, size_t *nfrag) {
	Clip *c;
	list<char*>::iterator it;
	size_t pos, begin, rem1, rem2, end, i, eob;
	
	assert(Clip::clip_map.count(id) > 0);
	c = Clip::clip_map[id];
	
	if (start >= c->rec_size) {
		return 0;
	}
	
	if (start + nbytes > c->rec_size) {
		nbytes = c->rec_size - start;
	} 
	
	begin = BLOCK_SIZE*(start/BLOCK_SIZE);
	rem1 = BLOCK_SIZE-(start-begin);
	rem2 = nbytes-(BLOCK_SIZE*(nbytes/BLOCK_SIZE));
	end = start+nbytes;
	
	*nfrag = (size_t)ceil(((double)nbytes)/BLOCK_SIZE);
	
	if (rem1 < rem2) {
		*nfrag += 1;
	}
	
	*data = (char **)calloc(*nfrag, sizeof(char *));
	*size = (size_t *)calloc(*nfrag, sizeof(size_t));
	
	for (it = c->blocks.begin(), pos = 0; 
			pos+BLOCK_SIZE < start; 
				it++, pos += BLOCK_SIZE);
	
	for (i=0, pos = start; 
			pos < end; 
				i++, it++) {
		(*data)[i] = (*it)+(pos%BLOCK_SIZE);
		eob = (size_t)BLOCK_SIZE*((pos/BLOCK_SIZE)+1);
		if (eob > end) {
			(*size)[i] = end-pos;
			break;
		} else {
			(*size)[i] = eob-pos;
		}
		pos = BLOCK_SIZE+(BLOCK_SIZE*(pos/BLOCK_SIZE));
	}
	
	return nbytes;
}

void soundrec_delete_clip(size_t id) {
	Clip *clip = Clip::clip_map[id];
	
	assert(clip != NULL);
	
	if (clip == cur) {
		assert(state != RECORDING);
		assert(state != PLAYING_BACK);
		cur = NULL;
	}
	
	delete clip;
}

void soundrec_set_inputs_cb(void (*cb)(list<Input*>&, map<uint32_t,update_t>&)) {
	user_inputs_cb = cb;
}

void soundrec_set_sources_cb(void (*cb)(list<Device*> &, list<Device*> &)) {
	user_sources_cb = cb;
}

void soundrec_set_pcm_cb(void (*cb)(size_t)) {
	user_pcm_cb = cb;
}
