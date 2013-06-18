

#include <list>
#include <string>
#include <bitset>
#include <cassert>
#include <cstring>

#include <gio/gio.h>
#include <glib.h>

#include "dconf.h"

using namespace std;

#define DBUS_SEND_FORMAT 	"dbus-send --dest=%s --type=method_call %s %s.%s"
#define QDBUS_FORMAT		"qdbus %s %s %s.%s"

#define DBUS_ADDR 			"org.SoundRecorder"
#define DBUS_IFACE			"/org/SoundRecorder"
#define DBUS_PATH			DBUS_ADDR

#define SETTINGS_PATH		"org.gnome.settings-daemon.plugins.media-keys"
#define KEYS_PATH			"/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings"

class Binding {
	public:
		string name;
		string binding;
		string command;
		static const char *format;
		Binding (const char *n, const char *b) : binding(b) {
			char *buf;
			
			name.reserve(50);
			buf = (char *)name.c_str();
			strcpy(buf, "SoundRecorder:");
			strncpy(buf+14, n, 36);
			
			command.reserve(120);
			buf = (char *)command.c_str();
			snprintf(buf, 120, format, DBUS_ADDR, DBUS_IFACE, DBUS_PATH, n);
		}
};	

const char *Binding::format = DBUS_SEND_FORMAT;

#define NBIND 6
static const char *binding_names[NBIND] = {"Record", "StopRecord", "Playback", "StopPlayback", "SwitchToSoundCard", "SwitchToMic"};

static GSettings *settings = NULL;
static DConfClient *dconf = NULL;

static list<Binding *> *get_key_bindings(const char *fname) {
	char *bindings[NBIND], *text, *line, *name, *binding, *sav1, *sav2;
	GError *err;
	list<Binding *> *user_bindings = NULL;
	bitset<NBIND> have_binding;
	
	if (g_file_get_contents(fname, &text, NULL, &err)) {
		line = strtok_r(text, "\n", &sav1);
		
		while (line != NULL) {
			name = strtok_r(line, "\t ", &sav2);
			binding = strtok_r(NULL, "\t ", &sav2);
			
			for (size_t i=0; i<NBIND; i++) {
				if (g_strcmp0(name, binding_names[i]) == 0 && binding != NULL) {
					bindings[i] = binding;
					have_binding[i] = true;
				}
			}
			line = strtok_r(NULL, "\n", &sav1);
		}
		
		if (have_binding.to_ulong() > 0) {
			user_bindings = new list<Binding*>();
			
			for (size_t i=0; i<NBIND; i++) {
				if (bindings[i] != NULL) {
					user_bindings->push_back(new Binding(binding_names[i], bindings[i]));
				}
			}
		}
		g_free(text);
		
	} else {
		fprintf(stderr, "%s\n", err->message);
		g_error_free(err);
	}
	
	return user_bindings;
}

static inline int get_custom_id(char *custom, size_t len) {
	char *ptr, *eptr;
	int id;
	
	assert(len > 2);
	
	for (ptr = custom+len-2; *ptr > 0x2f && *ptr < 0x3a; ptr++);
	ptr--;
	
	id = strtol(ptr, &eptr, 10);
	assert(eptr > ptr);
	
	return id;
}

static void clear_and_reload(bool do_both) {
	
	char **custom, *key, *user_custom, *custom_suffix;
	list<Binding *> *bindings;
	list<Binding *>::iterator it;
	list<pair<size_t,char*> > to_remove;
	list<pair<size_t,char*> >::iterator pit;
	GVariant *var;
	const char *str;
	size_t i, len, vlen, ncustom=0, nrem;
	int id, max_id = -1;
	Binding *b;
	
	if (settings == NULL) {
		settings = g_settings_new(SETTINGS_PATH);
	}
	if (dconf == NULL) {
		dconf = dconf_client_new();
	}
	
	custom = g_settings_get_strv(settings, "custom-keybindings");
	
	for (i=0; custom[i] != NULL; i++, ncustom++) {
		len = strlen(custom[i]);
		key = (char *)g_malloc(len+10);
		
		strcpy(key, custom[i]);
		strcpy(key+len, "name");
		
		var = dconf_client_read(dconf, key);
		if (var != NULL) {
			str = g_variant_get_string(var, &vlen);
			
			if (strncmp(str, "SoundRecorder:", 14) == 0) {
				key[len] = '\0';
				to_remove.push_back(pair<size_t,char*>(i,key));
			} else {
				g_free(key);
			}
			g_variant_unref(var);
		} else {
			key[len] = '\0';
			to_remove.push_back(pair<size_t,char*>(i,key));
		}
	}
	
	for (nrem=0, pit = to_remove.begin(); pit != to_remove.end(); pit++, nrem++) {
		g_free(custom[pit->first-nrem]);
		
		ncustom--;
		for (i=pit->first-nrem; i<ncustom; i++) {
			custom[i] = custom[i+1];
		}
		if (!dconf_client_write_fast(dconf, pit->second, NULL, NULL)) {
			printf("failed to queue write\n");
		}
		g_free(pit->second);
	}
	
	for (i=0; i<ncustom; i++) {
		id = get_custom_id(custom[i], len);
		if ((int)id > max_id) {
			max_id = id;
		}
	}
	
	if (do_both) {
		bindings = get_key_bindings("soundrec.keys");
	}
	
	if (bindings != NULL) {
		len = ncustom + bindings->size();
		custom = (char **)g_realloc(custom, (len+1)*sizeof(char *));
		custom[len] = NULL;
		
		for (i=ncustom, it = bindings->begin(); it != bindings->end(); i++, it++) {
			b = *it;
			
			user_custom = new char[120];
			snprintf(user_custom, 100, "%s/custom%d/", KEYS_PATH, ++max_id);
			custom[i] = user_custom;
			custom_suffix = user_custom + strlen(user_custom);
			
			strcpy(custom_suffix, "name");
			str = b->name.c_str();
			var = g_variant_new("s", str);
			if (!dconf_client_write_fast(dconf, user_custom, var, NULL)) {
				printf("write failed to queue\n");
			}
			
			strcpy(custom_suffix, "binding");
			str = b->binding.c_str();
			var = g_variant_new("s", str);
			if (!dconf_client_write_fast(dconf, user_custom, var, NULL)) {
				printf("write failed to queue\n");
			}
			
			strcpy(custom_suffix, "command");
			str = b->command.c_str();
			var = g_variant_new("s", str);
			if (!dconf_client_write_fast(dconf, user_custom, var, NULL)) {
				printf("write failed to queue\n");
			}
			
			*custom_suffix = '\0';
			delete b;
		}
		delete bindings;
		
		if (!g_settings_set_strv(settings, "custom-keybindings", custom)) {
			printf("failed to set custom-keybindings (key is not writeable)\n");
		}
		
	} else {
		printf("user bindings are null\n");
	}
	
	for (i=0; custom[i] != NULL; i++) {
		delete custom[i];
	}
	g_free(custom);
}

void soundrec_clear_bindings() {
	clear_and_reload(false);
	dconf_client_sync(dconf);
}

void soundrec_reload_bindings() {
	clear_and_reload(true);
}
