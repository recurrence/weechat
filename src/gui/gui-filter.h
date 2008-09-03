/*
 * Copyright (c) 2003-2008 by FlashCode <flashcode@flashtux.org>
 * See README for License detail, AUTHORS for developers list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __WEECHAT_GUI_FILTER_H
#define __WEECHAT_GUI_FILTER_H 1

#include <regex.h>

#define GUI_FILTER_TAG_NO_FILTER "no_filter"

/* filter structures */

struct t_gui_line;

struct t_gui_filter
{
    char *buffer;                      /* name of buffer                    */
    char *tags;                        /* tags                              */
    int tags_count;                    /* number of tags                    */
    char **tags_array;                 /* array of tags                     */
    char *regex;                       /* regex                             */
    regex_t *regex_prefix;             /* regex for line prefix             */
    regex_t *regex_message;            /* regex for line message            */
    struct t_gui_filter *prev_filter;  /* link to previous line             */
    struct t_gui_filter *next_filter;  /* link to next line                 */
};

/* filter variables */

extern struct t_gui_filter *gui_filters;
extern struct t_gui_filter *last_gui_filter;
extern int gui_filters_enabled;

/* filter functions */

extern int gui_filter_check_line (struct t_gui_buffer *buffer,
                                  struct t_gui_line *line);
extern void gui_filter_enable ();
extern void gui_filter_disable ();
extern struct t_gui_filter *gui_filter_search (const char *buffer,
                                               const char *tags,
                                               const char *regex);
extern struct t_gui_filter *gui_filter_search_by_number (int number);
extern struct t_gui_filter *gui_filter_new (const char *buffer,
                                            const char *tags,
                                            const char *regex);
extern void gui_filter_free (struct t_gui_filter *filter);
extern void gui_filter_free_all ();
extern int gui_filter_add_to_infolist (struct t_infolist *infolist,
                                       struct t_gui_filter *filter);
extern void gui_filter_print_log ();

#endif /* gui-filter.h */
