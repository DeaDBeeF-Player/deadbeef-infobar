/*
    Infobar plugin for DeaDBeeF music player
    Copyright (C) 2011 Dmitriy Simbiriatin <slpiv@mail.ru>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "infobar.h"

static int
uri_encode(char *out, int outl, const char *str, char space) {
	int l = outl;

    while (*str) {
        if (outl <= 1)
            return -1;

        if (!(
            (*str >= '0' && *str <= '9') ||
            (*str >= 'a' && *str <= 'z') ||
            (*str >= 'A' && *str <= 'Z') ||
            (*str == ' ') ||
            (*str == '\'') ||
            (*str == '/')
        ))
        {
            if (outl <= 3)
                return -1;

            snprintf (out, outl, "%%%02x", (uint8_t)*str);
            outl -= 3; str++; out += 3;
        }
        else {
        	if(*str == ' ' ||
        	   *str == '\''||
        	   *str == '/'
        	) {
        		*out = space;
        	} else {
        		*out = *str;
        	}
            out++; str++; outl--;
        }
    }
    *out = 0;
    return l - outl;
}

static gboolean
is_exists(const char *obj) {
	struct stat st;
	if(stat(obj, &st) != 0)
		return FALSE;

	return TRUE;
}

static gboolean
is_old_cache(const char *cache_file) {
	int res = -1;
	int upd_period = 0;
	time_t tm = time(NULL);

	struct stat st;
	res = stat(cache_file, &st);
	if(res == 0) {
		upd_period = deadbeef->conf_get_int(CONF_UPDATE_PERIOD, 24);
		if(upd_period > 0 && tm - st.st_mtime > upd_period * 60 * 60) {
			return TRUE;
		} else {
			return FALSE;
		}
	}
	return TRUE;
}

static char*
convert_to_utf8(const char *str, int size) {
	int res = -1;

	const char *str_cs = deadbeef->junk_detect_charset(str);
	if(!str_cs)
		return NULL;

	char *str_cnv = calloc(size * 4, sizeof(char));
	if(!str_cnv)
		return NULL;

	res = deadbeef->junk_iconv(str, size, str_cnv, size * 4, str_cs, "utf-8");
	if(res < 0) {
		free(str_cnv);
		return NULL;
	}

	return str_cnv;
}

static char*
load_content(const char *cache_file) {
	int res = -1;
	int cnt_size = 0;

	char *cnt = NULL;
	FILE *in_file = NULL;

	in_file = fopen(cache_file, "r");
	if(!in_file) {
		trace("failed to open content the %s\n", cache_file);
		goto cleanup;
	}

	res = fseek(in_file, 0, SEEK_END);
	if(res != 0)
		goto cleanup;

	cnt_size = ftell(in_file);
	rewind(in_file);

	cnt = calloc(cnt_size + 1, sizeof(char));
	if(!cnt)
		goto cleanup;

	res = fread(cnt, 1, cnt_size, in_file);
	if(res != cnt_size) {
		trace("failed to read the %s\n", cache_file);
	}

cleanup:
	if(in_file) fclose(in_file);
	return cnt;
}

static int
save_content(const char *cache_file, const char *buffer, int size) {
	int res = -1;
	int ret_value = 0;

	FILE *out_file = NULL;

	out_file = fopen(cache_file, "w+");
	if(!out_file) {
		trace("failed to open content file %s\n", cache_file);
		ret_value = -1;
		goto cleanup;
	}

	res = fwrite(buffer, 1, size, out_file);
	if(res <= 0) {
		trace("failed to write content %s\n", cache_file);
		ret_value = -1;
	}

cleanup:
	if(out_file) fclose(out_file);
	return ret_value;
}

static int
get_cache_path(char *cache_path, int size, ContentType type) {
	int res = -1;

	const char *home_cache = getenv("XDG_CACHE_HOME");

	switch(type) {
	case LYRICS:
		res = snprintf(cache_path, size, home_cache ? "%s/deadbeef/lyrics" : "%s/.cache/deadbeef/lyrics",
				home_cache ? home_cache : getenv("HOME"));
		break;
	case BIO:
		res = snprintf(cache_path, size, home_cache ? "%s/deadbeef/bio" : "%s/.cache/deadbeef/bio",
				home_cache ? home_cache : getenv("HOME"));
		break;
	}
	return res;
}

static void
parser_errors_handler(void *ctx, const char *msg, ...) {}

static char*
parse_content(const char *cnt, int size, const char *pattern, ContentType type) {
	int res = -1;

	xmlDocPtr doc = NULL;
	xmlXPathContextPtr ctx = NULL;
	xmlXPathObjectPtr obj = NULL;
	xmlNodePtr node = NULL;

	char *parsed_cnt = NULL;

	xmlSetGenericErrorFunc(NULL, parser_errors_handler);

	switch(type) {
	case HTML:
		doc = htmlReadMemory(cnt, size, NULL, "utf-8", (HTML_PARSE_RECOVER | HTML_PARSE_NONET));
		break;
	case XML:
		doc = xmlReadMemory(cnt, size, NULL, "utf-8", XML_PARSE_RECOVER | XML_PARSE_NONET);
		break;
	default:
		goto cleanup;
	}

	xmlSetGenericErrorFunc(NULL, NULL);

	if(!doc)
		goto cleanup;

	ctx = xmlXPathNewContext(doc);
	if(!ctx)
		goto cleanup;

	obj = xmlXPathEvalExpression((xmlChar*)pattern, ctx);
	if(!obj || !obj->nodesetval->nodeMax)
		goto cleanup;

	node = obj->nodesetval->nodeTab[0];
	if(!node)
		goto cleanup;

	parsed_cnt = (char*)xmlNodeGetContent(node);

cleanup:
	if(obj) xmlXPathFreeObject(obj);
	if(ctx) xmlXPathFreeContext(ctx);
	if(doc) xmlFreeDoc(doc);
	return parsed_cnt;
}

static char*
retrieve_txt_content(const char *url, int size) {
	int res = -1;

    char *txt = NULL;
    infobar_cnt = NULL;

    infobar_cnt = deadbeef->fopen(url);
    if(!infobar_cnt) {
    	trace("failed to open the %s\n", url);
    	goto cleanup;
    }

    txt = calloc(size + 1, sizeof(char));
    if(!txt)
    	goto cleanup;

    res = deadbeef->fread(txt, 1, size, infobar_cnt);
    if(res <= 0) {
    	trace("failed to retrieve a content from %s\n", url);
    }

cleanup:
    if(infobar_cnt) deadbeef->fclose(infobar_cnt);
    infobar_cnt = NULL;
    return txt;
}

static int
retrieve_img_content(const char *url, const char *img) {
	int res = -1;
	int ret_value  = 0;

	infobar_cnt = NULL;
	FILE *out_file = NULL;

	infobar_cnt = deadbeef->fopen(url);
	if(!infobar_cnt) {
		trace("failed to open the %s\n", url);
		ret_value = -1;
		goto cleanup;
	}

	out_file = fopen(img, "wb+");
	if(!out_file) {
		trace("failed to open the %s", img);
		ret_value = -1;
		goto cleanup;
	}

	int len = 0;
	char tmp[4096] = {0};

	while((len = deadbeef->fread(tmp, 1, sizeof(tmp), infobar_cnt)) > 0) {
		if(fwrite(tmp, 1, len, out_file) != len) {
			trace ("failed to write to the %s\n", img);
			ret_value = -1;
			break;
		}
	}

cleanup:
	if(out_file) fclose(out_file);
	if(infobar_cnt) deadbeef->fclose(infobar_cnt);
	infobar_cnt = NULL;
	return ret_value;
}

static int
retrieve_artist_bio(void) {
	trace("retrieving artist's bio\n");

	int res = -1;
	int ret_value = 0;

	char *bio = NULL;
	char *img_url = NULL;
	char *cnt = NULL;

	int bio_size = 0;
	int img_size = 0;
	int cnt_size = 0;

	deadbeef->mutex_lock(infobar_mutex);

	char cache_path[512] = {0};
	res = get_cache_path(cache_path, sizeof(cache_path), BIO);
	if(res == 0) {
		trace("failed to retrieve bio cache dir\n");
		ret_value = -1;
		goto cleanup;
	}

	if(!is_exists(cache_path)) {
		res = mkdir(cache_path, 0755);
		if(res < 0) {
			trace("failed to create bio cache dir\n");
			ret_value = -1;
			goto cleanup;
		}
	}

	char cache_file[512] = {0};
	res = snprintf(cache_file, sizeof(cache_file), "%s/%s", cache_path, eartist_lfm);
	if(res == 0) {
		trace("failed to form a path to the bio cache file\n");
		ret_value = -1;
		goto cleanup;
	}

	char cache_img[512] = {0};
	res = snprintf(cache_img, sizeof(cache_img), "%s/%s_img", cache_path, eartist_lfm);
	if(res == 0) {
		trace("failed to form a path to the bio image file\n");
		ret_value = -1;
		goto cleanup;
	}

	deadbeef->conf_lock();
	const char *locale = deadbeef->conf_get_str_fast(CONF_BIO_LOCALE, "en");
	deadbeef->conf_unlock();

	char track_url[512] = {0};
	res = snprintf(track_url, sizeof(track_url), "http://ws.audioscrobbler.com/2.0/?method=artist.getinfo&artist=%s&lang=%s&api_key=b25b959554ed76058ac220b7b2e0a026",
			eartist_lfm, locale);
	if(res == 0) {
		trace("failed to form bio download url\n");
		ret_value = -1;
		goto cleanup;
	}

	if(!is_exists(cache_file) ||
			is_old_cache(cache_file) ||
			(strcmp(old_locale, locale) != 0 &&
			 strcmp(old_locale, "") != 0)) {
		cnt = retrieve_txt_content(track_url, TXT_MAX);
		if(!cnt) {
			trace("failed to download artist's bio\n");
			ret_value = -1;
			goto cleanup;
		}

		cnt_size = strlen(cnt);
		bio = parse_content(cnt, cnt_size, "/lfm/artist/bio/content", XML);
		if(bio) {
			bio_size = strlen(bio);

			char *tmp = parse_content(bio, bio_size, "/html/body", HTML);
			if(tmp) {
				free(bio);
				bio = tmp;
				bio_size = strlen(bio);
			}

			res = save_content(cache_file, bio, bio_size);
			if(res < 0) {
				trace("failed to save retrieved bio\n");
				ret_value = -1;
				goto cleanup;
			}
		}
	} else {
		bio = load_content(cache_file);
		if(bio) {
			bio_size = strlen(bio);
		}
	}

	memset(old_locale, 0, sizeof(old_locale));
	strncpy(old_locale, locale, sizeof(old_locale));

	if(!cnt) {
		cnt = retrieve_txt_content(track_url, TXT_MAX);
		if(!cnt) {
			trace("failed to download artist's bio\n");
			ret_value = -1;
			goto cleanup;
		}
	}

	cnt_size = strlen(cnt);
	img_url = parse_content(cnt, cnt_size, "//image[@size=\"extralarge\"]", XML);
	if(img_url) {
		img_size = strlen(img_url);
	}

	if(img_url && img_size > 0) {
		if(!is_exists(cache_img)) {
			res = retrieve_img_content(img_url, cache_img);
			if(res < 0) {
				trace("failed to download artist's image\n");
				ret_value = -1;
				goto cleanup;
			}
		}
	}

cleanup:
	update_bio_view(bio, bio_size, cache_img);

	if(cnt) free(cnt);
	if(bio) free(bio);
	if(img_url) free(img_url);
	deadbeef->mutex_unlock(infobar_mutex);
	return ret_value;
}

static char*
fetch_lyrics_from(const char *url, const char *artist, const char *title, const char *cache_file, const char *pattern) {
	int res = -1;

	char *cnt = NULL;
	char *lyrics = NULL;

	int cnt_size = 0;
	int lyrics_size = 0;

	char track_url[512] = {0};
	res = snprintf(track_url, sizeof(track_url), url, artist, title);
	if(res == 0) {
		trace("failed to form lyrics download url\n");
		goto cleanup;
	}

	if(!is_exists(cache_file) ||
			is_old_cache(cache_file)) {
		cnt = retrieve_txt_content(track_url, TXT_MAX);
		if(!cnt) {
			trace("failed to download a track's lyrics\n");
			goto cleanup;
		}

		cnt_size = strlen(cnt);
		lyrics = parse_content(cnt, cnt_size, pattern, HTML);
		if(lyrics) {
			lyrics_size = strlen(lyrics);

			char *tmp = convert_to_utf8(lyrics, lyrics_size);
			if(tmp) {
				free(lyrics);
				lyrics = tmp;
				lyrics_size = strlen(lyrics);
			}

			res = save_content(cache_file, lyrics, lyrics_size);
			if(res < 0) {
				trace("failed to save retrieved lyrics\n");
				goto cleanup;
			}
		}
	}

cleanup:
	if(cnt) free(cnt);
	return lyrics;
}

static int
retrieve_track_lyrics(void) {
	trace("retrieving track's lyrics\n");

	int res = -1;
	int ret_value = 0;
	int lyrics_size = 0;

	char *lyrics = NULL;

	gboolean time = FALSE;
	gboolean mania = FALSE;

	deadbeef->mutex_lock(infobar_mutex);

	char cache_path[512] = {0};
	res = get_cache_path(cache_path, sizeof(cache_path), LYRICS);
	if(res == 0) {
		trace("failed to retrieve lyrics cache dir\n");
		ret_value = -1;
		goto cleanup;
	}

	if(!is_exists(cache_path)) {
		res = mkdir(cache_path, 0755);
		if(res < 0) {
			trace("failed to create lyrics cache dir\n");
			ret_value = -1;
			goto cleanup;
		}
	}

	char cache_file[512] = {0};
	res = snprintf(cache_file, sizeof(cache_file), "%s/%s-%s", cache_path, eartist, etitle);
	if(res == 0) {
		trace("failed to form a path to the lyrics cache file\n");
		ret_value = -1;
		goto cleanup;
	}

	mania = deadbeef->conf_get_int(CONF_LYRICSMANIA_ENABLED, 1);
	if(mania && !lyrics && lyrics_size == 0) {
		lyrics = fetch_lyrics_from("http://www.lyricsmania.com/%s_lyrics_%s.html",
				etitle, eartist, cache_file, "//*[@id=\"songlyrics_h\"]");
		if(lyrics) {
			lyrics_size = strlen(lyrics);
		}
	}

	time = deadbeef->conf_get_int(CONF_LYRICSTIME_ENABLED, 1);
	if(time && !lyrics && lyrics_size == 0) {
		lyrics = fetch_lyrics_from("http://www.lyricstime.com/%s-%s-lyrics.html",
				eartist, etitle, cache_file, "//*[@id=\"songlyrics\"]");
		if(lyrics) {
			lyrics_size = strlen(lyrics);
		}
	}

	if(!lyrics && lyrics_size == 0) {
		lyrics = load_content(cache_file);
		if(lyrics) {
			lyrics_size = strlen(lyrics);
		}
	}

cleanup:
	update_lyrics_view(lyrics, lyrics_size);

	if(lyrics) free(lyrics);
	deadbeef->mutex_unlock(infobar_mutex);
	return ret_value;
}

static void
infobar_songchanged() {
	if(infobar_cnt) {
		deadbeef->fabort(infobar_cnt);
		infobar_cnt = NULL;
	}
}

static void
infobar_songstarted(ddb_event_track_t *ev) {
	trace("infobar song started\n");

	int res = -1;

	if(!deadbeef->conf_get_int(CONF_LYRICS_ENABLED, 1) &&
			!deadbeef->conf_get_int(CONF_BIO_ENABLED, 1))
		return;

	const char *cur_artist = deadbeef->pl_find_meta(ev->track, "artist");
	const char *cur_title =  deadbeef->pl_find_meta(ev->track, "title");

	if(!cur_artist || !cur_title)
		return;

	deadbeef->mutex_lock(infobar_mutex);

	memset(artist, 0, sizeof(artist));
	strncpy(artist, cur_artist, sizeof(artist));

	memset(title, 0, sizeof(title));
	strncpy(title, cur_title, sizeof(title));

	memset(eartist, 0, sizeof(eartist));
    res = uri_encode(eartist, sizeof(eartist), artist, '_');
    if(res == -1) {
    	deadbeef->mutex_unlock(infobar_mutex);
    	return;
    }

	memset(etitle, 0, sizeof(etitle));
	res = uri_encode(etitle, sizeof (etitle), title, '_');
	if(res == -1) {
		deadbeef->mutex_unlock(infobar_mutex);
		return;
	}

	memset(eartist_lfm, 0, sizeof(eartist_lfm));
	res = uri_encode(eartist_lfm, sizeof(eartist_lfm), artist, '+');
	if(res == - 1) {
		deadbeef->mutex_unlock(infobar_mutex);
		return;
	}

	deadbeef->mutex_unlock(infobar_mutex);
	deadbeef->cond_signal(infobar_cond);
}

static void
infobar_thread(void *ctx) {
	for(;;) {
		trace("infobar_thread started\n");

		if(infobar_stopped) {
			deadbeef->mutex_unlock(infobar_mutex);
			return;
		}

		deadbeef->cond_wait(infobar_cond, infobar_mutex);
		if(infobar_stopped) {
			deadbeef->mutex_unlock(infobar_mutex);
			return;
		}

		deadbeef->mutex_unlock(infobar_mutex);

		if(deadbeef->conf_get_int(CONF_LYRICS_ENABLED, 1)) {
			trace("retrieving song's lyrics...\n");
			retrieve_track_lyrics();
		}

		if(deadbeef->conf_get_int(CONF_BIO_ENABLED, 1)) {
			trace("retrieving artist's bio...\n");
			retrieve_artist_bio();
		}
	}
}

static void
infobar_config_changed(void) {
	gboolean state = FALSE;

    gdk_threads_enter ();

	state = deadbeef->conf_get_int(CONF_LYRICS_ENABLED, 1);
	if(lyrics_toggle && lyrics_tab)
		set_tab_visible(lyrics_toggle, lyrics_tab, state);

	state = deadbeef->conf_get_int(CONF_BIO_ENABLED, 1);
	if(bio_toggle && bio_tab)
		set_tab_visible(bio_toggle, bio_tab, state);

    gdk_threads_leave ();
}

static void
update_bio_view(const char *bio, int size, const char *img_file) {
    GtkTextIter begin, end;

    gdk_threads_enter();

    if(bio_image)
    	gtk_image_set_from_file(GTK_IMAGE(bio_image), img_file);

    if(bio_buffer) {
    	gtk_text_buffer_get_iter_at_line (bio_buffer, &begin, 0);
    	gtk_text_buffer_get_end_iter (bio_buffer, &end);
    	gtk_text_buffer_delete (bio_buffer, &begin, &end);

    	if(bio && size > 0) {
    		gtk_text_buffer_insert_with_tags_by_name(bio_buffer,
    				&begin, bio, -1, "text", NULL);
    	} else {
    		gtk_text_buffer_insert_with_tags_by_name(bio_buffer,
    				&begin, "Biography not found.", -1, "text", NULL);
    	}
    }

    gdk_threads_leave();
}

static void
update_lyrics_view(const char *lyrics, int size) {
    GtkTextIter begin, end;

    gdk_threads_enter ();

    if(lyrics_buffer) {
    	gtk_text_buffer_get_iter_at_line (lyrics_buffer, &begin, 0);
    	gtk_text_buffer_get_end_iter (lyrics_buffer, &end);
    	gtk_text_buffer_delete (lyrics_buffer, &begin, &end);


    	gtk_text_buffer_insert_with_tags_by_name(lyrics_buffer,
    			&begin, title, -1, "title_name", NULL);

    	gtk_text_buffer_insert(lyrics_buffer, &begin, "\n", -1);

    	gtk_text_buffer_insert_with_tags_by_name(lyrics_buffer,
    			&begin, artist, -1, "title_artist", NULL);

    	gtk_text_buffer_insert(lyrics_buffer, &begin, "\n", -1);

    	if(lyrics && size > 0) {
    		gtk_text_buffer_insert_with_tags_by_name(lyrics_buffer,
    				&begin, lyrics, -1, "text", NULL);
    	} else {
    		gtk_text_buffer_insert_with_tags_by_name(lyrics_buffer,
    				&begin, "Lyrics not found.", -1, "text", NULL);
    	}
    }

    gdk_threads_leave ();
}

static void
set_tab_visible(GtkWidget *toggle, GtkWidget *item, gboolean visible) {
	if(visible) {
		gtk_widget_show(toggle);
		gtk_widget_show(item);
	} else {
		gtk_widget_hide(toggle);
		gtk_widget_hide(item);
	}
}

static void
infobar_menu_toggle(GtkMenuItem *item, gpointer data) {
    gboolean state = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
    gtk_widget_set_visible(infobar, state);
    deadbeef->conf_set_int(CONF_INFOBAR_VISIBLE, state);
}

static void
infobar_tab_changed(GtkToggleButton *toggle, GtkWidget *widget) {
	gint index = gtk_notebook_page_num(GTK_NOTEBOOK(infobar_tabs), widget);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(infobar_tabs), index);
}

static int
create_infobar_interface(void) {
	trace("modifying player's inteface\n");

	int res = -1;
	int ret_value = 0;

	gdk_threads_enter();

	res = create_infobar();
	if(res < 0) {
		trace("failed to create infobar\n");
		ret_value = -1;
		goto end;
	}

	GtkWidget *ddb_main = lookup_widget(gtkui_plugin->get_mainwin(), "vbox1");
	GtkWidget *ddb_tabs= lookup_widget(gtkui_plugin->get_mainwin(), "tabstrip");
	GtkWidget *ddb_playlist = lookup_widget(gtkui_plugin->get_mainwin(), "frame1");

	g_object_ref(ddb_tabs);
	g_object_ref(ddb_playlist);

	GtkWidget *playlist_box = gtk_vbox_new(FALSE, 0);

	gtk_container_remove(GTK_CONTAINER(ddb_main), ddb_tabs);
	gtk_container_remove(GTK_CONTAINER(ddb_main), ddb_playlist);

	gtk_box_pack_start(GTK_BOX(playlist_box), ddb_tabs, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(playlist_box), ddb_playlist, TRUE, TRUE, 0);

	g_object_unref(ddb_tabs);
	g_object_unref(ddb_playlist);

	GtkWidget *ddb_main_new = gtk_hpaned_new();

	gtk_paned_pack1(GTK_PANED(ddb_main_new), playlist_box, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(ddb_main_new), infobar, FALSE, TRUE);

	gtk_container_add(GTK_CONTAINER(ddb_main), ddb_main_new);

	gtk_box_reorder_child(GTK_BOX(ddb_main), ddb_main_new, 2);

	gtk_widget_show_all (ddb_main_new);

	gboolean state = deadbeef->conf_get_int(CONF_INFOBAR_VISIBLE, 0);
	gtk_widget_set_visible(infobar, state);

end:
	gdk_threads_leave();
	return ret_value;
}

static int
create_infobar_menu_entry(void) {
    trace("creating infobar menu entry\n");

    GtkWidget *menu_item = gtk_check_menu_item_new_with_mnemonic ("_Infobar");
    if(!menu_item)
    	return -1;

    GtkWidget *view_menu = lookup_widget(gtkui_plugin->get_mainwin(), "View_menu");

    gtk_container_add(GTK_CONTAINER(view_menu), menu_item);

    gboolean state = deadbeef->conf_get_int(CONF_INFOBAR_VISIBLE, 0);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item), state);

    g_signal_connect(menu_item, "activate", G_CALLBACK(infobar_menu_toggle), NULL);
    gtk_widget_show(menu_item);

	return 0;
}

static int
create_infobar(void) {
	trace("creating infobar\n");

	if(infobar)
		return 0;

	infobar = gtk_vbox_new(FALSE, 0);

	infobar_tabs = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(infobar_tabs), FALSE);

	GtkWidget *infobar_toggles = gtk_hbox_new(FALSE, 0);

	lyrics_toggle = gtk_radio_button_new_with_label(NULL, "Lyrics");
	gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(lyrics_toggle), FALSE);

	bio_toggle = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(lyrics_toggle), "Biography");
	gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(bio_toggle), FALSE);

	lyrics_tab = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(lyrics_tab),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	GtkWidget *bio_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(bio_scroll),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	GtkWidget *lyrics_view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(lyrics_view), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(lyrics_view), GTK_WRAP_WORD);

	GtkWidget *bio_view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(bio_view), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(bio_view), GTK_WRAP_WORD);

	lyrics_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lyrics_view));
	bio_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bio_view));

	bio_tab = gtk_vbox_new(FALSE, 0);
	bio_image = gtk_image_new();

	gtk_text_buffer_create_tag(lyrics_buffer, "title_name",
							   "weight", PANGO_WEIGHT_BOLD,
							   "font", "11",
			 	 	 	 	   "left_margin", 5,
			 	 	 	 	   NULL);

	gtk_text_buffer_create_tag(lyrics_buffer, "title_artist",
	 			 	 	 	 	"style", PANGO_STYLE_ITALIC,
	 			 	 	 	 	"left_margin", 5,
	 			 	 	 	 	NULL);

	gtk_text_buffer_create_tag(lyrics_buffer, "text",
			 	 	 	 	   "left_margin", 5,
			 	 	 	 	   NULL);

	gtk_text_buffer_create_tag(bio_buffer, "text",
	 			 	 	 	   "left_margin", 5,
	 			 	 	 	   NULL);

	gtk_container_add(GTK_CONTAINER(lyrics_tab), lyrics_view);
	gtk_container_add(GTK_CONTAINER(bio_scroll), bio_view);

	gtk_box_pack_start(GTK_BOX(bio_tab), bio_image, FALSE, FALSE, 1);
	gtk_box_pack_start(GTK_BOX(bio_tab), bio_scroll, TRUE, TRUE, 1);

	gtk_box_pack_start(GTK_BOX(infobar_toggles), lyrics_toggle, FALSE, FALSE, 1);
	gtk_box_pack_start(GTK_BOX(infobar_toggles), bio_toggle, FALSE, FALSE, 1);

	gtk_notebook_append_page(GTK_NOTEBOOK(infobar_tabs), lyrics_tab, NULL);
	gtk_notebook_append_page(GTK_NOTEBOOK(infobar_tabs), bio_tab, NULL);

	g_signal_connect(lyrics_toggle, "toggled", G_CALLBACK(infobar_tab_changed), lyrics_tab);
	g_signal_connect(bio_toggle, "toggled", G_CALLBACK(infobar_tab_changed), bio_tab);

	gtk_box_pack_start(GTK_BOX(infobar), infobar_toggles, FALSE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(infobar), infobar_tabs, TRUE, TRUE, 1);

	int width = deadbeef->conf_get_int(CONF_INFOBAR_WIDTH, 250);
	gtk_widget_set_size_request(infobar, width, -1);

	gtk_widget_show_all(infobar);
	return 0;
}

static int
infobar_message(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2) {
	switch(id) {
	case DB_EV_SONGSTARTED:
		infobar_songstarted((ddb_event_track_t*) ctx);
		break;
	case DB_EV_SONGCHANGED:
		infobar_songchanged();
		break;
	case DB_EV_CONFIGCHANGED:
		infobar_config_changed();
		break;
	}
	return 0;
}

static gboolean
infobar_init(void) {
	trace("starting up infobar plugin\n");

	int res = -1;

	res = create_infobar_interface();
	if(res < 0)
		return TRUE;

	res = create_infobar_menu_entry();
	if(res < 0)
		return TRUE;

	infobar_config_changed();

	return FALSE;
}

static int
infobar_connect(void) {
	trace("connecting infobar plugin\n");

	gtkui_plugin = (ddb_gtkui_t*)deadbeef->plug_get_for_id("gtkui");
	if(!gtkui_plugin)
		return -1;

	g_idle_add((GSourceFunc)infobar_init, NULL);
	return 0;
}

static int
infobar_disconnect(void) {
	trace("disconnecting infobar plugin\n");
	gtkui_plugin = NULL;
	return 0;
}

static int
infobar_start(void) {
	trace("starting infobar plugin\n");

	infobar_stopped = FALSE;

	infobar_cond = deadbeef->cond_create();
	infobar_mutex = deadbeef->mutex_create_nonrecursive();
	infobar_tid = deadbeef->thread_start_low_priority(infobar_thread, NULL);
	return 0;
}

static int
infobar_stop(void) {
	trace("stopping infobar plugin\n");

	infobar_stopped = TRUE;

	if(infobar_cnt) {
		deadbeef->fabort(infobar_cnt);
		infobar_cnt = NULL;
	}

	if(infobar_tid) {
		deadbeef->cond_signal(infobar_cond);
		deadbeef->thread_join(infobar_tid);
		infobar_tid = 0;
	}

	if(infobar_mutex) {
		deadbeef->mutex_free(infobar_mutex);
		infobar_mutex = 0;
	}

	if(infobar_cond) {
		deadbeef->cond_free(infobar_cond);
		infobar_cond = 0;
	}

	return 0;
}

static const char settings_dlg[] =
    "property \"Enable lyrics\" checkbox infobar.lyrics.enabled 1;"
	"property \"Fetch from lyricsmania\" checkbox infobar.lyrics.lyricsmania 1;"
	"property \"Fetch from lyrics.time\" checkbox infobar.lyrics.lyricstime 1;"
	"property \"Enable biography\" checkbox infobar.bio.enabled 1;"
	"property \"Biography locale\" entry infobar.bio.locale \"en\";"
	"property \"Cache update period (hr)\" entry infobar.cache.period 24;"
	"property \"Default sidebar width (px)\" entry infobar.width 250;"
;

static DB_misc_t plugin = {
	.plugin.api_vmajor = DB_API_VERSION_MAJOR,
    .plugin.api_vminor = DB_API_VERSION_MINOR,
    .plugin.version_major = 1,
    .plugin.version_minor = 0,
    .plugin.type = DB_PLUGIN_MISC,
    .plugin.name = "Infobar plugin",
    .plugin.descr = "Fetches and shows song's lyrics and artist's biography.\n\n"
    				"To change the biography's locale, set an appropriate ISO 639-2 locale code.\n"
    				"See http://en.wikipedia.org/wiki/List_of_ISO_639-2_codes for more infomation.",
    .plugin.copyright =
        "Copyright (C) 2011 Dmitriy Simbiriatin <slpiv@mail.ru>\n"
        "\n"
        "This program is free software; you can redistribute it and/or\n"
        "modify it under the terms of the GNU General Public License\n"
        "as published by the Free Software Foundation; either version 2\n"
        "of the License, or (at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
        "GNU General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU General Public License\n"
        "along with this program; if not, write to the Free Software\n"
        "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n"
    ,
    .plugin.website = "https://bitbucket.org/Not_eXist/deadbeef-infobar/overview",
    .plugin.start = infobar_start,
    .plugin.stop = infobar_stop,
    .plugin.connect	= infobar_connect,
    .plugin.disconnect = infobar_disconnect,
    .plugin.configdialog = settings_dlg,
    .plugin.message = infobar_message,
};

DB_plugin_t *infobar_load (DB_functions_t *ddb) {
    deadbeef = ddb;
    return DB_PLUGIN(&plugin);
}
