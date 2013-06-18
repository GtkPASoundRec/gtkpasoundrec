
#include <ctime>
#include <map>
#include <cassert>
#include <cstring>

#include <gtk/gtk.h>
#include <glib.h>

#include "soundrec.hpp"
#include "soundrec_dbus.hpp"
#include "soundrec_dconf.hpp"

#define UIFILE "SoundRecorder.ui"
#define CSSFILE "soundrec.css"

using namespace std;

class ClipData {
	public:
		char *id;
		char *ts;
		time_t start;
		time_t latest;
		size_t clipid;
		int n;
		GtkTreeIter iter;
		ClipData(int num, size_t cid) : id(new char[10]), ts(new char[10]), 
				start(time(NULL)), latest(start), clipid(cid), n(num) {
			snprintf(id, 10, "Clip%d", num);
			this->format_time();
		}
		void format_time() {
			int s = latest-start;
			int m = s/60;
			int t = s-(m*60);
			snprintf(ts,10,"%01d:%02d",m,t);
		}
		~ClipData() {
			delete id;
			delete ts;
		}
};

GtkContainer *source_window;

GtkWidget *record_button;
GtkWidget *playback_button;
GtkWidget *save_button;
GtkWidget *monitor_button;
GtkWidget *mic_button;
GtkWidget *app_button;

GtkWidget *progress_bar;

map<size_t,ClipData*> clip_map;
int nclips = 0;

GtkListStore *clip_list;
GtkListStore *input_list;
GtkListStore *monitor_list;
GtkListStore *mic_list;

GtkTreeView *clip_view;
GtkTreeView *input_view;
GtkTreeView *monitor_view;
GtkTreeView *mic_view;
	
GtkTreeSelection *clip_select;
GtkTreeSelection *input_select;
GtkTreeSelection *monitor_select;
GtkTreeSelection *mic_select;

GtkWidget *err_label;
GtkWidget *err_dialog;
GtkWidget *prefix_entry;
GtkWidget *path_entry;
	
GtkWidget *save_fc;

gboolean recording_cb(void *data) {
	rec_state state = soundrec_get_state();
	ClipData *dat = (ClipData *)data;
	time_t now = time(NULL);
	
	if (state != RECORDING) {
		return FALSE;
	}
	
	if (now == dat->latest) {
		return TRUE;
	}
	
	dat->latest = now;
	dat->format_time();
	
	gtk_list_store_set(clip_list, &(dat->iter), 1, dat->ts, -1);
	
	return TRUE;
}

void add_clip(size_t id) {
	ClipData *dat = new ClipData(++nclips, id);
	
	gtk_list_store_prepend(clip_list, &(dat->iter));
	gtk_list_store_set(clip_list, &(dat->iter), 0, dat->id, 1, dat->ts, 2, (gpointer)dat, -1);
	
	clip_map[id] = dat;
	
	g_timeout_add(100, recording_cb, (void *)dat);
}

Recordable *get_record_input() {
	GtkTreeIter iter;
	GtkTreeModel *model;
	Recordable *rec = NULL;
	
	if (gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(app_button))) {
		if (gtk_tree_selection_get_selected(input_select, &model, &iter)) {
			gtk_tree_model_get(model, &iter, 4, &rec, -1);
		} else {
			if (gtk_tree_model_get_iter_first(model, &iter)) {
				gtk_tree_model_get(model, &iter, 4, &rec, -1);
				gtk_tree_selection_select_iter(input_select, &iter);
			}
		}
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(monitor_button))) {
		if (gtk_tree_selection_get_selected(monitor_select, &model, &iter)) {
			gtk_tree_model_get(model, &iter, 1, &rec, -1);
		} else {
			if (gtk_tree_model_get_iter_first(model, &iter)) {
				gtk_tree_model_get(model, &iter, 1, &rec, -1);
				gtk_tree_selection_select_iter(monitor_select, &iter);
			}
		}
	} else {
		if (gtk_tree_selection_get_selected(mic_select, &model, &iter)) {
			gtk_tree_model_get(model, &iter, 1, &rec, -1);
		} else {
			if (gtk_tree_model_get_iter_first(model, &iter)) {
				gtk_tree_model_get(model, &iter, 1, &rec, -1);
				gtk_tree_selection_select_iter(mic_select, &iter);
			}
		}
	}
	
	return rec;
}

void on_record(GtkButton *) {
	rec_state state = soundrec_get_state();
	Recordable *rec;
	size_t id;
	switch (state) {
		case IDLE:
			rec = get_record_input();
			if (rec != NULL) {
				id = soundrec_start_recording(rec);
				add_clip(id);
				gtk_button_set_label(GTK_BUTTON(record_button), "Stop");
			} else {
				printf("Can't record: no input selected\n");
			}
			break;
		case RECORDING:
			gtk_button_set_label(GTK_BUTTON(record_button), "Record");
			soundrec_stop_recording();
			break;
		case PLAYING_BACK:
			printf("Can't record: Is playing back\n");
		default:;
	}
}

size_t get_selected_clip() {
	GtkTreeIter iter;
	GtkTreeModel *model;
	ClipData *dat;
	
	if (gtk_tree_selection_get_selected(clip_select, &model, &iter)) {
		gtk_tree_model_get(model, &iter, 2, &dat, -1);
		return dat->clipid;
	}
	return (size_t)-1;
}

size_t get_first_clip() {
	GtkTreeIter iter;
	GtkTreeModel *model;
	ClipData *dat;
	
	model = GTK_TREE_MODEL(clip_list);
	if (gtk_tree_model_get_iter_first( model, &iter)) {
		gtk_tree_model_get(model, &iter, 2, &dat, -1);
		return dat->clipid;
	}
	return (size_t)-1;
}

size_t get_selected_or_first_clip() {
	size_t id = get_selected_clip();
	if (id == (size_t)-1) {
		id = get_first_clip();
	}
	return id;
}

gboolean playback_timeout(void *data) {
	rec_state state = soundrec_get_state();
	double pct;
	
	if (state == PLAYING_BACK) {
		pct = soundrec_get_progress();
		gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(progress_bar), pct);
		return TRUE;
	}
	
	gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(progress_bar), 0.0);
	gtk_button_set_label( GTK_BUTTON(playback_button), "Playback");
	return FALSE;
}

void on_playback(GtkButton *) {
	rec_state state = soundrec_get_state();
	size_t id;
	switch (state) {
		case IDLE:
			id = get_selected_or_first_clip();
			if (id != (size_t)-1) {
				gtk_button_set_label(GTK_BUTTON(playback_button), "Stop");
				g_timeout_add(100, playback_timeout, NULL);
				soundrec_start_playback(id);
			} else {
				printf("No clip recorded\n");
			}
			break;
		case PLAYING_BACK:
			gtk_button_set_label(GTK_BUTTON(playback_button), "Playback");
			soundrec_stop_playback();
			break;
		case RECORDING:
			printf("Can't begin playback: Is recording\n");
		default:;
	}
}

void on_save(GtkButton *) {
	rec_state state = soundrec_get_state();
	char *f, *name;
	size_t id, len;
	GtkWidget *toplevel, *dialog;
	ClipData *clip;
	bool new_name = false;
	
	if (state == RECORDING) {
		printf("Can't save clip: Is recording\n");
		return;
	}
	
	id = get_selected_clip();
	if (id == (size_t)-1) {
		printf("No clip selected\n");
		return;
	}
	
	assert(clip_map.count(id) > 0);
	clip = clip_map[id];
	
	len = strlen(clip->id);
	
	if (strcasestr(clip->id,".wav") != (clip->id+len-4)) {
		name = new char[len+5];
		snprintf(name, len+5, "%s.wav", clip->id);
		new_name = true;
	} else {
		name = clip->id;
	}
	
	toplevel = gtk_widget_get_toplevel (GTK_WIDGET(save_button));
	//GtkFileFilter *filter = gtk_file_filter_new();
	
	dialog = gtk_file_chooser_dialog_new("Save Clip",
			GTK_WINDOW( toplevel),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
			NULL);
	
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
	gtk_file_chooser_set_current_name( GTK_FILE_CHOOSER(dialog), name);
			
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		f = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		soundrec_save_clip(f, id);
		g_free (f);
	}
	
	gtk_widget_destroy (dialog);
	
	if (new_name) {
		delete name;
	}
}

gboolean save_all_delete(GtkWidget *dialog) {
	gtk_widget_hide(dialog);
	
	return TRUE;
}

void save_all_file_set(GtkFileChooserButton *button) {
	char *fname;
	
	fname = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(save_fc));
	gtk_entry_set_text( GTK_ENTRY(path_entry), fname);
	
	g_free(fname);
}

void save_all_ok(GtkButton *ok, GtkDialog *dialog) {
	map<size_t, ClipData*>::iterator it;
	const char *prefix, *path;
	char *pbuf, *buf;
	size_t plen, len;
	list<char *> fnames;
	list<char *>::iterator fnit;
	bool fail = false;
	
	path = gtk_entry_get_text( GTK_ENTRY(path_entry));
	
	if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
		gtk_label_set_text( GTK_LABEL(err_label), "Not a directory");
		
		gtk_dialog_run( GTK_DIALOG(err_dialog));
		gtk_widget_hide(err_dialog);
		
		return;
	}
			
	prefix = gtk_entry_get_text( GTK_ENTRY(prefix_entry));
	plen = strlen(path)+strlen(prefix)+2;
	pbuf = (char *)malloc(plen);
	
	snprintf(pbuf, plen, "%s/%s", path, prefix);
	
	for (it = clip_map.begin(); it != clip_map.end(); it++) {
		len = plen+20;
		buf = (char *)malloc(len);
	
		snprintf(buf, len, "%s%d.wav", pbuf, it->second->n);
		
		if (g_file_test(buf, G_FILE_TEST_EXISTS)) {
			gtk_label_set_text( GTK_LABEL(err_label), "File already exists");
			
			gtk_dialog_run( GTK_DIALOG(err_dialog));
			gtk_widget_hide(err_dialog);
			
			fail = true;
			break;
		}
		
		fnames.push_back(buf);
	}
	
	for (it = clip_map.begin(), fnit = fnames.begin(); 
			it != clip_map.end() && fnit != fnames.end(); 
				it++, fnit++) {
		if (!fail) {
			soundrec_save_clip(*fnit, it->first); 
			if (!g_file_test(*fnit, G_FILE_TEST_EXISTS)) {
				gtk_label_set_text( GTK_LABEL(err_label), "Failed to save");
				
				gtk_dialog_run( GTK_DIALOG(err_dialog));
				gtk_widget_hide(err_dialog);
				
				fail = true;
			}
		}
		free(*fnit);
	}
	
	if (!fail) {
		gtk_widget_hide( GTK_WIDGET(dialog));
	}
	
	free(pbuf);
}

void save_all_cancel(GtkButton *cancel, GtkDialog *dialog) {
	gtk_widget_hide( GTK_WIDGET(dialog));
}

void on_save_all(GtkButton *save, GtkDialog *dialog) {
	rec_state state = soundrec_get_state();
	char *fname;
	GtkTreeIter iter;
	
	if (state == RECORDING) {
		printf("Can't save clip: Is recording\n");
		return;
	}
	
	if (gtk_tree_model_get_iter_first( GTK_TREE_MODEL(clip_list), &iter)) {
		fname = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(save_fc));
		
		gtk_entry_set_text ( GTK_ENTRY(path_entry), fname);
		gtk_entry_set_text ( GTK_ENTRY(prefix_entry), "Clip");
		
		g_free(fname);
		
		gtk_widget_show( GTK_WIDGET(dialog));
	}
}

void on_clear(GtkButton *clear) {
	rec_state state = soundrec_get_state();
	size_t id = get_selected_clip();
	size_t id_cur = get_first_clip();
	ClipData *dat;
	
	if (id == (size_t)-1) {
		printf("No clip selected\n");
		return;
	}
	
	if (state == RECORDING || state == PLAYING_BACK) {
		if (id == id_cur) {
			printf("Can't clear: Is recording or playing back\n");
			return;
		}
	}
	
	dat = clip_map[id];
	gtk_list_store_remove(clip_list, &(dat->iter));
	soundrec_delete_clip(id);
	delete dat;
	clip_map.erase(id);
}

void on_clear_all(GtkButton *button, GtkDialog *dialog) {
	rec_state state = soundrec_get_state();
	GtkTreeIter iter;
	gint resp;
	map<size_t, ClipData*>::iterator it;

	if (state == RECORDING || state == PLAYING_BACK) {
		printf("Can't clear: Is recording or playing back\n");
		return;
	}
	
	if (gtk_tree_model_get_iter_first( GTK_TREE_MODEL(clip_list), &iter)) {
		resp = gtk_dialog_run(dialog);
		
		if (resp == GTK_RESPONSE_YES) {
			gtk_list_store_clear(clip_list);
			
			for (it = clip_map.begin(); it != clip_map.end(); it++) {
				soundrec_delete_clip(it->first);
				delete it->second;
			}
			
			clip_map.clear();
		}
	
		gtk_widget_hide( GTK_WIDGET(dialog));
	}
}

static void switch_to_sound_card() {
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(monitor_button), TRUE);
}

static void switch_to_mic() {
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(mic_button), TRUE);
}

void create_sources_model(GtkTreeView **view, GtkListStore **list, GtkTreeSelection **select) {
	GtkCellRenderer *cell = gtk_cell_renderer_text_new();
	
	// 1: pointer
	*list = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);
	*view = GTK_TREE_VIEW(gtk_tree_view_new_with_model( GTK_TREE_MODEL(*list)));
	*select = gtk_tree_view_get_selection(*view);
	
	gtk_tree_selection_set_mode(*select, GTK_SELECTION_SINGLE);
	
	gtk_tree_view_set_headers_visible(*view, FALSE);
		
	gtk_tree_view_insert_column_with_attributes( *view, -1, "Name", cell, "text", 0, NULL);
}

void edited_cb(GtkCellRendererText *cell, char *path, char *text, void *) {
	GtkTreeIter iter;
	ClipData *data;
	
	if (gtk_tree_model_get_iter_from_string( GTK_TREE_MODEL(clip_list), &iter, path)) {
		
		if (strlen(text) > 0) {
			gtk_list_store_set(clip_list, &iter, 0, text, -1);
			gtk_tree_model_get( GTK_TREE_MODEL(clip_list), &iter, 2, &data, -1);
			
			delete data->id;
			data->id = strdup(text);
		}
	}
}

void create_clips_model(GtkTreeView *view, GtkListStore **list) {
	GtkCellRenderer *edit = gtk_cell_renderer_text_new();
	GtkCellRenderer *cell = gtk_cell_renderer_text_new();
	
	g_object_set(edit, "editable", TRUE, NULL);
	g_signal_connect(edit, "edited", G_CALLBACK(edited_cb), NULL);
	
	*list = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	
	gtk_tree_view_set_headers_visible(view, FALSE);
	
	gtk_tree_view_insert_column_with_attributes( view, -1, "Clip", edit, "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes( view, -1, "Length", cell, "text", 1, NULL);
	
	gtk_tree_view_set_model(view, GTK_TREE_MODEL(*list));
}

void create_inputs_model(GtkTreeView *view, GtkListStore **list) {
	GtkCellRenderer *cell = gtk_cell_renderer_text_new();
	GtkCellRenderer *pix = gtk_cell_renderer_pixbuf_new();
	
	*list = gtk_list_store_new(5, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	
	gtk_tree_view_set_headers_visible(view, FALSE);
	
	//gtk_tree_view_insert_column_with_attributes( view, -1, "Sink", cell, "text", 0, NULL);
	//gtk_tree_view_insert_column_with_attributes( view, -1, "Index", cell, "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes( view, -1, "Icon", pix, "icon-name", 3, NULL);
	gtk_tree_view_insert_column_with_attributes( view, -1, "Name", cell, "text", 2, NULL);
	
	gtk_tree_view_set_model(view, GTK_TREE_MODEL(*list));
}

void set_sources_view(GtkWidget *view) {
	GtkWidget *child = gtk_bin_get_child( GTK_BIN(source_window));
	
	assert(child != view);
		
	gtk_container_remove( source_window, child);
	gtk_container_add( source_window, view);
	
	gtk_widget_show(view);
}

void on_toggled(GtkToggleButton *toggle, GtkWidget *view) {
	if (gtk_toggle_button_get_active(toggle)) {
		set_sources_view(view);
	}
}

void update_sources_list(list<Device*> &dev, GtkTreeView *view, GtkListStore *l, GtkTreeSelection *select) {
	GtkTreeIter iter;
	list<Device*>::iterator it; 
	char *name;
	size_t len;
	
	gtk_list_store_clear(l);
	
	for (it = dev.begin(); it != dev.end(); it++) {
		string &ns = (*it)->name;
		len = ns.length();
		name = (char *)ns.c_str();
		
		if (len > 8) {
			if (strcmp(name+len-8, ".monitor") == 0) {
				name = strndup(name, len-8);
			}
		}
		
		gtk_list_store_append( l, &iter);
		gtk_list_store_set( l, &iter, 0, name, 1, (*it), -1);
		
		if (name != ns.c_str()) {
			free(name);
		}
	}
}

void sources_cb(list<Device*> &monitors, list<Device*> &mics) {
	update_sources_list(monitors, monitor_view, monitor_list, monitor_select);	
	update_sources_list(mics, mic_view, mic_list, mic_select);
	
	set_sources_view( GTK_WIDGET(monitor_view));
}

void inputs_cb(list<Input*> &inputs, map<uint32_t, update_t> &update_map) {
	GtkTreeIter iter;
	GtkTreeModel *model;
	list<Input*>::iterator it;
	uint32_t idx;
	gboolean valid;
	Input *inp;
	update_t upd;
	const char *name;
	
	model = GTK_TREE_MODEL(input_list);
	
	valid = gtk_tree_model_get_iter_first(model, &iter);
	
	while (valid) {
		gtk_tree_model_get(model, &iter, 1, &idx,-1);
		upd = update_map[idx];
		
		if (upd == CHANGE || upd == REMOVE) {
			valid = gtk_list_store_remove(input_list, &iter);
		} else {
			valid = gtk_tree_model_iter_next(model, &iter);
		}
	}
	
	for (it = inputs.begin(); it != inputs.end(); it++) {
		inp = (*it);
		upd = update_map[inp->index];
		
		if (inp->props.count("application.name") == 0) {
			name = "Unknown";
		} else {
			name = inp->props["application.name"].c_str();
		}
		
		if (upd == NEW || upd == CHANGE) {
			gtk_list_store_append(input_list, &iter);
			gtk_list_store_set(input_list, &iter, 
				0, inp->sink, 
				1, inp->index, 
				2, name,
				3, inp->props["application.icon_name"].c_str(),
				4, inp, -1);
		}
	}
}

void parsing_cb(GtkCssProvider *css, GtkCssSection *sec, GError *err, void *data) {
	printf("Parsing error\n");
}

void apply_style(const char *path) {
	GtkCssProvider *css;
	GdkDisplay *display;
	GdkScreen *screen;
	
	css = gtk_css_provider_new();
	display = gdk_display_get_default ();
	screen = gdk_display_get_default_screen (display);
	
	gtk_style_context_add_provider_for_screen(screen, 
		GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	
	g_signal_connect(css, "parsing-error", (void (*)())parsing_cb, NULL);
	
	gtk_css_provider_load_from_path(css, path, NULL);
	
	g_object_unref(css);
}

int main(int argc, char **argv) {
	GtkBuilder *builder;
	
	GtkWidget *window;
	GtkWidget *clear_dialog;
	GtkWidget *save_dialog;
	
	GtkWidget *save_all_button;
	GtkWidget *clear_button;
	GtkWidget *clear_all_button;
	
	GtkWidget *save_dialog_button;
	GtkWidget *cancel_button;
	
	gtk_init(&argc, &argv);
	
	builder = gtk_builder_new ();
	gtk_builder_add_from_file (builder, UIFILE, NULL);
	
	apply_style(CSSFILE);
	
	window = GTK_WIDGET (gtk_builder_get_object (builder, "window1"));
	
	clear_dialog = GTK_WIDGET (gtk_builder_get_object (builder, "ClearDialog"));
	save_dialog = GTK_WIDGET (gtk_builder_get_object (builder, "SaveDialog"));
	
	gtk_widget_set_name(window, "main-window");
	gtk_widget_set_name(save_dialog, "save-dialog");
	
	source_window = GTK_CONTAINER (gtk_builder_get_object (builder, "SourceWindow"));
	
	record_button = GTK_WIDGET (gtk_builder_get_object (builder, "RecordButton"));
	playback_button = GTK_WIDGET (gtk_builder_get_object (builder, "PlaybackButton"));
	save_button = GTK_WIDGET (gtk_builder_get_object (builder, "SaveButton"));
	save_all_button = GTK_WIDGET (gtk_builder_get_object (builder, "SaveAllButton"));
	clear_button = GTK_WIDGET (gtk_builder_get_object (builder, "ClearButton"));
	clear_all_button = GTK_WIDGET (gtk_builder_get_object (builder, "ClearAllButton"));
	monitor_button = GTK_WIDGET (gtk_builder_get_object (builder, "MonitorButton"));
	mic_button = GTK_WIDGET (gtk_builder_get_object (builder, "MicButton"));
	app_button = GTK_WIDGET (gtk_builder_get_object (builder, "AppButton"));
	progress_bar = GTK_WIDGET (gtk_builder_get_object (builder, "ProgressBar"));
	err_label = GTK_WIDGET (gtk_builder_get_object (builder, "ErrorLabel"));
	err_dialog = GTK_WIDGET (gtk_builder_get_object (builder, "ErrorDialog"));
	path_entry = GTK_WIDGET (gtk_builder_get_object (builder, "PathEntry"));
	prefix_entry = GTK_WIDGET (gtk_builder_get_object (builder, "PrefixEntry"));
	save_fc = GTK_WIDGET (gtk_builder_get_object (builder, "FileChooser"));
	save_dialog_button = GTK_WIDGET (gtk_builder_get_object (builder, "SaveDialogButton"));
	cancel_button = GTK_WIDGET (gtk_builder_get_object (builder, "CancelButton"));
	
	g_object_ref(G_OBJECT(clear_dialog));
	g_object_ref(G_OBJECT(save_dialog));
	g_object_ref(G_OBJECT(err_dialog));
	
	clip_view = GTK_TREE_VIEW (gtk_builder_get_object (builder, "ClipView"));
	input_view = GTK_TREE_VIEW (gtk_builder_get_object (builder, "InputView"));
	
	create_clips_model(clip_view, &clip_list);
	create_inputs_model(input_view, &input_list);
	create_sources_model(&monitor_view, &monitor_list, &monitor_select);
	create_sources_model(&mic_view, &mic_list, &mic_select);
	
	g_object_ref(G_OBJECT(input_view));
	g_object_ref(G_OBJECT(monitor_view));
	g_object_ref(G_OBJECT(mic_view));
	
	clip_select = GTK_TREE_SELECTION (gtk_builder_get_object (builder, "ClipSelection"));
	input_select = GTK_TREE_SELECTION (gtk_builder_get_object (builder, "InputSelection"));
	
	g_object_unref(builder);
	
	g_signal_connect (record_button, "clicked", G_CALLBACK (on_record), NULL);
	g_signal_connect (playback_button, "clicked", G_CALLBACK (on_playback), NULL);
	g_signal_connect (save_button, "clicked", G_CALLBACK (on_save), NULL);
	g_signal_connect (clear_button, "clicked", G_CALLBACK (on_clear), NULL);
	g_signal_connect (clear_all_button, "clicked", G_CALLBACK (on_clear_all), clear_dialog);
	g_signal_connect (save_all_button, "clicked", G_CALLBACK (on_save_all), save_dialog);
	g_signal_connect (monitor_button, "toggled", G_CALLBACK (on_toggled), monitor_view);
	g_signal_connect (mic_button, "toggled", G_CALLBACK (on_toggled), mic_view);
	g_signal_connect (app_button, "toggled", G_CALLBACK (on_toggled), input_view);
	g_signal_connect (save_dialog_button, "clicked", G_CALLBACK (save_all_ok), save_dialog);
	g_signal_connect (cancel_button, "clicked", G_CALLBACK (save_all_cancel), save_dialog);
	g_signal_connect (save_fc, "file-set", G_CALLBACK (save_all_file_set), NULL);
	g_signal_connect (save_dialog, "delete-event", G_CALLBACK (save_all_delete), NULL);
	//g_signal_connect (input_selection, "changed", G_CALLBACK (on_input_changed), NULL);
	
	g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
	
	soundrec_set_inputs_cb(inputs_cb);
	soundrec_set_sources_cb(sources_cb);
	soundrec_set_dbus_cb(G_CALLBACK(on_record), G_CALLBACK(on_playback), 
		G_CALLBACK(switch_to_sound_card), G_CALLBACK(switch_to_mic));
		
	soundrec_init();
	
	soundrec_reload_bindings();
	soundrec_dbus_connect();
	
	gtk_widget_show(window);
	gtk_main();
	
	return 0;
}

