
#include <cstring>
#include <cassert>

#include <gio/gio.h>
#include <glib.h>

#include "soundrec.hpp"

using namespace std;

static GCallback record = NULL;
static GCallback playback = NULL;
static GCallback switch_to_sound_card = NULL;
static GCallback switch_to_mic = NULL;

static size_t owner_id;
static GDBusConnection *connection = NULL;

static void handle_method_call(GDBusConnection *con, 
		const gchar *sender, 
		const gchar *path, 
		const gchar *iname, 
		const gchar *mname,
		GVariant *param, GDBusMethodInvocation *inv, gpointer user_data) 
{
	rec_state state = soundrec_get_state();
	
	if (strcmp(mname, "Record") == 0) {
		if (record != NULL && state == IDLE) {
			record();
		}
	} else if (strcmp(mname, "StopRecord") == 0) {
		if (record != NULL && state == RECORDING) {
			record();
		}
	} else if (strcmp(mname, "Playback") == 0) {
		if (playback != NULL && state == IDLE) {
			playback();
		}
	} else if (strcmp(mname, "StopPlayback") == 0) {
		if (playback != NULL && state == PLAYING_BACK) {
			playback();
		}
	} else if (strcmp(mname, "SwitchToSoundCard") == 0) {
		if (switch_to_sound_card != NULL) {
			switch_to_sound_card();
		}
	} else if (strcmp(mname, "SwitchToMic") == 0) {
		if (switch_to_mic != NULL) {
			switch_to_mic();
		}
	} else {
		printf("got unknown method: %s\n", mname);
	}
	
	g_dbus_method_invocation_return_value(inv, NULL);
}

/* for now */
static const GDBusInterfaceVTable itable = {
  handle_method_call, NULL, NULL
};

static void name_acquired(GDBusConnection *c, const gchar *name, void *) {
	GDBusNodeInfo *idata = NULL;
	gchar *xml;
	GError *err = NULL;
	guint reg_id;
	
	assert(c != NULL);
	connection = c;
	
	if (g_file_get_contents("soundrec_dbus.xml", &xml, NULL, &err)) {
		idata = g_dbus_node_info_new_for_xml (xml, NULL);
		g_assert (idata != NULL);
		
		reg_id = g_dbus_connection_register_object(connection, 
			idata->path, idata->interfaces[0], &itable, NULL, NULL, NULL);
		g_assert (reg_id > 0);
		
		g_free(xml);
	} else {
		fprintf(stderr, "%s\n", err->message);
		g_error_free(err);
	}
}

static void name_lost(GDBusConnection *c, const gchar *name, void *) {
	const char *str = " (name exists)";
	if (c == NULL) {
		str = " (disconnected)";
	}
	connection = NULL;
	printf("failed to get name: %s%s\n", name, str );
}

void soundrec_set_dbus_cb(GCallback rec, GCallback play, GCallback switch_card, GCallback switch_mic) {
	record = rec;
	playback = play;
	switch_to_sound_card = switch_card;
	switch_to_mic = switch_mic;
}

void soundrec_dbus_connect() {
	owner_id = g_bus_own_name(G_BUS_TYPE_SESSION, "org.SoundRecorder", 
		G_BUS_NAME_OWNER_FLAGS_NONE, NULL, name_acquired, name_lost, NULL, NULL);
}
