
#ifndef _SOUNDREC_DBUS_HEADER_
#define _SOUNDREC_DBUS_HEADER_

#include <glib.h>

void soundrec_set_dbus_cb(GCallback rec, GCallback play, GCallback switch_card, GCallback switch_mic);
void soundrec_dbus_connect();

#endif
