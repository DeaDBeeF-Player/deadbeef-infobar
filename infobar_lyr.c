/*
    Infobar plugin for DeaDBeeF music player
    Copyright (C) 2011-2012 Dmitriy Simbiriatin <dmitriy.simbiriatin@gmail.com>

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

#include "infobar_lyr.h"

static int
form_lyr_url(const char *artist, const char* title, const char* template, char **url) {
    
    char *eartist = NULL, *etitle = NULL;
    
    if (lyr_uri_encode(artist, title, &eartist, &etitle) == -1)
        return -1;

    if (asprintf(url, template, eartist, etitle) == -1) {
        free(eartist);
        free(etitle);
        return -1;
    }
    free(eartist);
    free(etitle);
    return 0;
}

static int
form_script_cmd(const char *artist, const char* title, const char *script, const char* template, char **cmd) {
    
    char *eartist = NULL, *etitle = NULL;
    
    if (lyr_uri_encode(artist, title, &eartist, &etitle) == -1)
        return -1;
    
    if (asprintf(cmd, template, script, eartist, etitle) == -1) {
        free(eartist);
        free(etitle);
        return -1;
    }
    free(eartist);
    free(etitle);
    return 0;
}

static int
fetch_lyrics(const char *url, const char *pattern, ContentType type, char **txt) {
    
    char *raw_page = NULL;
    if (retrieve_txt_content(url, &raw_page) == -1)
        return -1;
    
    char *lyr_txt = NULL;
    if (parse_content(raw_page, pattern, &lyr_txt, type, 0) == -1) {
        free(raw_page);
        return -1;
    }
    free(raw_page);
    *txt = lyr_txt;
    
    char *lyr_utf8 = NULL;
    if (deadbeef->junk_detect_charset(lyr_txt)) {
        if (convert_to_utf8(lyr_txt, &lyr_utf8) == 0) {
            free(lyr_txt);
            *txt = lyr_utf8;
        }
    }
    return 0;
}

int fetch_lyrics_from_lyricsmania(const char *artist, const char *title, char **txt) {
    
    char *url = NULL;
    if (form_lyr_url(artist, title, LYRICSMANIA_URL_TEMPLATE, &url) == -1)
        return -1;
    
    char *lyr_txt = NULL;
    if (fetch_lyrics(url, LYRICSMANIA_PATTERN, HTML, &lyr_txt) == -1) {
        free(url);
        return -1;
    }
    free(url);
    *txt = lyr_txt;
    return 0;
}

int fetch_lyrics_from_lyricstime(const char *artist, const char *title, char **txt) {
    
    char *url = NULL;
    if (form_lyr_url(artist, title, LYRICSTIME_URL_TEMPLATE, &url) == -1)
        return -1;
    
    char *lyr_txt = NULL;
    if (fetch_lyrics(url, LYRICSTIME_PATTERN, HTML, &lyr_txt) == -1) {
        free(url);
        return -1;
    }
    free(url);
    *txt = lyr_txt;
    return 0;
}

int fetch_lyrics_from_megalyrics(const char *artist, const char *title, char **txt) {
    
    char *url = NULL;
    if (form_lyr_url(artist, title, MEGALYRICS_URL_TEMPLATE, &url) == -1)
        return -1;
    
    char *lyr_txt = NULL;
    if (fetch_lyrics(url, MEGALYRICS_PATTERN, HTML, &lyr_txt) == -1) {
        free(url);
        return -1;
    }
    free(url);
    *txt = lyr_txt;
    return 0;
}

int fetch_lyrics_from_lyricswikia(const char *artist, const char *title, char **txt) {
    
    char *url = NULL;
    if (form_lyr_url(artist, title, LYRICSWIKIA_URL_TEMPLATE, &url) == -1)
        return -1;

    char *raw_page = NULL;
    if (fetch_lyrics(url, LYRICSWIKIA_XML_PATTERN, XML, &raw_page) == -1) {
        free(url);
        return -1;
    }
    free(url);

    if (is_redirect(raw_page)) {
        
        char *rartist = NULL;
        char *rtitle = NULL;
        
        if (get_redirect_info(raw_page, &rartist, &rtitle) == 0) {
            
            free(raw_page);
            
            url = NULL;
            if (form_lyr_url(rartist, rtitle, LYRICSWIKIA_URL_TEMPLATE, &url) == -1) {
                free(rartist);
                free(rtitle);
                return -1;
            }
            free(rartist);
            free(rtitle);
            
            raw_page = NULL;
            if (fetch_lyrics(url, LYRICSWIKIA_XML_PATTERN, XML, &raw_page) == -1) {
                free(url);
                return -1;
            }
            free(url);
        }
    }
    char *fst_lyr_txt = NULL;
    char *snd_lyr_txt = NULL;

    if (parse_content(raw_page, LYRICSWIKIA_HTML_PATTERN, &fst_lyr_txt, HTML, 0) == -1) {
        free(raw_page);
        return -1;
    }
    *txt = fst_lyr_txt;

    char *multi_lyr = NULL;
    if (parse_content(raw_page, LYRICSWIKIA_HTML_PATTERN, &snd_lyr_txt, HTML, 1) == 0) {
        
        if (concat_lyrics(fst_lyr_txt, snd_lyr_txt, &multi_lyr) == 0) {
            free(fst_lyr_txt);
            *txt = multi_lyr;
        }
        free(snd_lyr_txt);
    }
    free(raw_page);
    return 0;
}

int fetch_lyrics_from_script(const char *artist, const char *title, char **txt) {

    deadbeef->conf_lock();
    
    const char *path = deadbeef->conf_get_str_fast(CONF_LYRICS_SCRIPT_PATH, "");
    
    char *cmd = NULL;
    if (form_script_cmd(artist, title, path, SCRIPT_CMD_TEMPLATE, &cmd) == -1) {
        deadbeef->conf_unlock();
        return -1;
    }
    deadbeef->conf_unlock();
    
    if (execute_script(cmd, txt) == -1) {
        free(cmd);
        return -1;
    }
    free(cmd);
    
    char *txt_utf8 = NULL;
    if (deadbeef->junk_detect_charset(*txt)) {
        if (convert_to_utf8(*txt, &txt_utf8) == 0) {
            free(*txt);
            *txt = txt_utf8;
        }
    }
    return 0;
}
