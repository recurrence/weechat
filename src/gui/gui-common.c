/*
 * Copyright (c) 2003-2005 by FlashCode <flashcode@flashtux.org>
 * See README for License detail, AUTHORS for developers list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* gui-common.c: display functions, used by any GUI */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <curses.h>

#include "../common/weechat.h"
#include "gui.h"
#include "../common/command.h"
#include "../common/weeconfig.h"
#include "../common/hotlist.h"
#include "../common/log.h"
#include "../irc/irc.h"


int gui_init_ok = 0;                        /* = 1 if GUI is initialized    */
int gui_ok = 0;                             /* = 1 if GUI is ok             */
                                            /* (0 when term size too small) */
int gui_add_hotlist = 1;                    /* 0 is for temporarly disable  */
                                            /* hotlist add for all buffers  */

t_gui_window *gui_windows = NULL;           /* pointer to first window      */
t_gui_window *last_gui_window = NULL;       /* pointer to last window       */
t_gui_window *gui_current_window = NULL;    /* pointer to current window    */

t_gui_buffer *gui_buffers = NULL;           /* pointer to first buffer      */
t_gui_buffer *last_gui_buffer = NULL;       /* pointer to last buffer       */
t_gui_buffer *buffer_before_dcc = NULL;     /* buffer before dcc switch     */
t_gui_infobar *gui_infobar;                 /* pointer to infobar content   */


/*
 * gui_window_new: create a new window
 */

t_gui_window *
gui_window_new (int x, int y, int width, int height)
{
    t_gui_window *new_window;
    
    #ifdef DEBUG
    wee_log_printf ("Creating new window (x:%d, y:%d, width:%d, height:%d)\n",
                    x, y, width, height);
    #endif
    if ((new_window = (t_gui_window *)(malloc (sizeof (t_gui_window)))))
    {
        new_window->win_x = x;
        new_window->win_y = y;
        new_window->win_width = width;
        new_window->win_height = height;
        
        new_window->win_chat_x = 0;
        new_window->win_chat_y = 0;
        new_window->win_chat_width = 0;
        new_window->win_chat_height = 0;
        new_window->win_chat_cursor_x = 0;
        new_window->win_chat_cursor_y = 0;
        
        new_window->win_nick_x = 0;
        new_window->win_nick_y = 0;
        new_window->win_nick_width = 0;
        new_window->win_nick_height = 0;
        new_window->win_nick_start = 0;
        
        new_window->win_input_x = 0;
        
        new_window->win_title = NULL;
        new_window->win_chat = NULL;
        new_window->win_nick = NULL;
        new_window->win_status = NULL;
        new_window->win_infobar = NULL;
        new_window->win_input = NULL;
        new_window->win_separator = NULL;
        
        new_window->textview_chat = NULL;
        new_window->textbuffer_chat = NULL;
        new_window->texttag_chat = NULL;
        new_window->textview_nicklist = NULL;
        new_window->textbuffer_nicklist = NULL;
        
        new_window->dcc_first = NULL;
        new_window->dcc_selected = NULL;
        new_window->dcc_last_displayed = NULL;
        
        new_window->buffer = NULL;
        
        new_window->first_line_displayed = 0;
        new_window->sub_lines = 0;
        
        /* add window to windows queue */
        new_window->prev_window = last_gui_window;
        if (gui_windows)
            last_gui_window->next_window = new_window;
        else
            gui_windows = new_window;
        last_gui_window = new_window;
        new_window->next_window = NULL;
    }
    else
        return NULL;
    
    return new_window;
}

/*
 * gui_buffer_new: create a new buffer in current window
 */

t_gui_buffer *
gui_buffer_new (t_gui_window *window, void *server, void *channel, int dcc,
                int switch_to_buffer)
{
    t_gui_buffer *new_buffer;
    
    #ifdef DEBUG
    wee_log_printf ("Creating new buffer\n");
    #endif
    
    /* use first buffer if no server was assigned to this buffer */
    if (!dcc && gui_buffers && (!SERVER(gui_buffers)))
    {
        if (server)
            ((t_irc_server *)(server))->buffer = gui_buffers;
        if (channel)
            ((t_irc_channel *)(channel))->buffer = gui_buffers;
        gui_buffers->server = server;
        gui_buffers->channel = channel;
        if (cfg_log_auto_server)
            log_start (gui_buffers);
        return gui_buffers;
    }
    
    if ((new_buffer = (t_gui_buffer *)(malloc (sizeof (t_gui_buffer)))))
    {
        new_buffer->num_displayed = 0;
        new_buffer->number = (last_gui_buffer) ? last_gui_buffer->number + 1 : 1;
        
        /* assign server and channel to buffer */
        new_buffer->server = server;
        new_buffer->channel = channel;
        new_buffer->dcc = dcc;
        /* assign buffer to server and channel */
        if (server && !channel)
            SERVER(new_buffer)->buffer = new_buffer;
        if (channel)
            CHANNEL(new_buffer)->buffer = new_buffer;
        
        if (!window->buffer)
        {
            window->buffer = new_buffer;
            window->first_line_displayed = 1;
            window->sub_lines = 0;
            gui_calculate_pos_size (window);
            gui_window_init_subwindows (window);
        }
        
        /* init lines */
        new_buffer->lines = NULL;
        new_buffer->last_line = NULL;
        new_buffer->num_lines = 0;
        new_buffer->line_complete = 1;
        
        /* notify level */
        new_buffer->notify_level = channel_get_notify_level (server, channel);
        
        /* create/append to log file */
        new_buffer->log_filename = NULL;
        new_buffer->log_file = NULL;
        if ((cfg_log_auto_server && BUFFER_IS_SERVER(new_buffer))
            || (cfg_log_auto_channel && BUFFER_IS_CHANNEL(new_buffer))
            || (cfg_log_auto_private && BUFFER_IS_PRIVATE(new_buffer)))
            log_start (new_buffer);
        
        /* init input buffer */
        new_buffer->input_buffer_alloc = INPUT_BUFFER_BLOCK_SIZE;
        new_buffer->input_buffer = (char *) malloc (INPUT_BUFFER_BLOCK_SIZE);
        new_buffer->input_buffer[0] = '\0';
        new_buffer->input_buffer_size = 0;
        new_buffer->input_buffer_pos = 0;
        new_buffer->input_buffer_1st_display = 0;
        
        /* init completion */
        completion_init (&(new_buffer->completion));
        
        /* init history */
        new_buffer->history = NULL;
        new_buffer->last_history = NULL;
        new_buffer->ptr_history = NULL;
        new_buffer->num_history = 0;
        
        /* add buffer to buffers queue */
        new_buffer->prev_buffer = last_gui_buffer;
        if (gui_buffers)
            last_gui_buffer->next_buffer = new_buffer;
        else
            gui_buffers = new_buffer;
        last_gui_buffer = new_buffer;
        new_buffer->next_buffer = NULL;
        
        /* switch to new buffer */
        if (switch_to_buffer)
            gui_switch_to_buffer (window, new_buffer);
        
        /* redraw buffer */
        gui_redraw_buffer (new_buffer);
    }
    else
        return NULL;
    
    return new_buffer;
}

/*
 * gui_buffer_clear: clear buffer content
 */

void
gui_buffer_clear (t_gui_buffer *buffer)
{
    t_gui_window *ptr_win;
    t_gui_line *ptr_line;
    t_gui_message *ptr_message;
    
    while (buffer->lines)
    {
        ptr_line = buffer->lines->next_line;
        while (buffer->lines->messages)
        {
            ptr_message = buffer->lines->messages->next_message;
            if (buffer->lines->messages->message)
                free (buffer->lines->messages->message);
            free (buffer->lines->messages);
            buffer->lines->messages = ptr_message;
        }
        free (buffer->lines);
        buffer->lines = ptr_line;
    }
    
    buffer->lines = NULL;
    buffer->last_line = NULL;
    buffer->num_lines = 0;
    buffer->line_complete = 1;
    
    for (ptr_win = gui_windows; ptr_win; ptr_win = ptr_win->next_window)
    {
        if (ptr_win->buffer == buffer)
        {
            ptr_win->first_line_displayed = 1;
            ptr_win->sub_lines = 0;
        }
    }
    
    if (buffer == gui_current_window->buffer)
        gui_draw_buffer_chat (buffer, 1);
}

/*
 * gui_buffer_clear_all: clear all buffers content
 */

void
gui_buffer_clear_all ()
{
    t_gui_buffer *ptr_buffer;
    
    for (ptr_buffer = gui_buffers; ptr_buffer; ptr_buffer = ptr_buffer->next_buffer)
        gui_buffer_clear (ptr_buffer);
}

/* 
 * gui_infobar_printf: display message in infobar
 */

void
gui_infobar_printf (int time_displayed, int color, char *message, ...)
{
    static char buffer[1024];
    va_list argptr;
    t_gui_infobar *ptr_infobar;
    char *pos, *buf2;
    
    va_start (argptr, message);
    vsnprintf (buffer, sizeof (buffer) - 1, message, argptr);
    va_end (argptr);
    
    buf2 = weechat_convert_encoding (cfg_look_charset_decode,
                                     (cfg_look_charset_internal && cfg_look_charset_internal[0]) ?
                                     cfg_look_charset_internal : local_charset,
                                     buffer);
    
    ptr_infobar = (t_gui_infobar *)malloc (sizeof (t_gui_infobar));
    if (ptr_infobar)
    {
        ptr_infobar->color = color;
        ptr_infobar->text = strdup (buf2);
        pos = strchr (ptr_infobar->text, '\n');
        if (pos)
            pos[0] = '\0';
        ptr_infobar->remaining_time = (time_displayed <= 0) ? -1 : time_displayed;
        ptr_infobar->next_infobar = gui_infobar;
        gui_infobar = ptr_infobar;
        gui_draw_buffer_infobar (gui_current_window->buffer, 1);
    }
    else
        wee_log_printf (_("Not enough memory for infobar message\n"));
    
    free (buf2);
}

/*
 * gui_window_free: delete a window
 */

void
gui_window_free (t_gui_window *window)
{
    if (window->buffer && (window->buffer->num_displayed > 0))
        window->buffer->num_displayed--;
    
    /* remove window from windows list */
    if (window->prev_window)
        window->prev_window->next_window = window->next_window;
    if (window->next_window)
        window->next_window->prev_window = window->prev_window;
    if (gui_windows == window)
        gui_windows = window->next_window;
    if (last_gui_window == window)
        last_gui_window = window->prev_window;
    
    free (window);
}

/*
 * gui_infobar_remove: remove last displayed message in infobar
 */

void
gui_infobar_remove ()
{
    t_gui_infobar *new_infobar;
    
    if (gui_infobar)
    {
        new_infobar = gui_infobar->next_infobar;
        if (gui_infobar->text)
            free (gui_infobar->text);
        free (gui_infobar);
        gui_infobar = new_infobar;
    }
}

/*
 * gui_line_free: delete a line from a buffer
 */

void
gui_line_free (t_gui_line *line)
{
    t_gui_message *ptr_message;
    
    while (line->messages)
    {
        ptr_message = line->messages->next_message;
        if (line->messages->message)
            free (line->messages->message);
        free (line->messages);
        line->messages = ptr_message;
    }
    free (line);
}

/*
 * gui_buffer_free: delete a buffer
 */

void
gui_buffer_free (t_gui_buffer *buffer, int switch_to_another)
{
    t_gui_window *ptr_win;
    t_gui_buffer *ptr_buffer;
    t_gui_line *ptr_line;
    int create_new;
    
    create_new = (buffer->server || buffer->channel);
    
    hotlist_remove_buffer (buffer);
    if (hotlist_initial_buffer == buffer)
        hotlist_initial_buffer = NULL;
    
    if (buffer_before_dcc == buffer)
        buffer_before_dcc = NULL;
    
    if (switch_to_another)
    {
        for (ptr_win = gui_windows; ptr_win; ptr_win = ptr_win->next_window)
        {
            if ((buffer == ptr_win->buffer) &&
                ((buffer->next_buffer) || (buffer->prev_buffer)))
                gui_switch_to_previous_buffer (ptr_win);
        }
    }
    
    /* decrease buffer number for all next buffers */
    for (ptr_buffer = buffer->next_buffer; ptr_buffer; ptr_buffer = ptr_buffer->next_buffer)
    {
        ptr_buffer->number--;
    }
    
    /* free lines and messages */
    while (buffer->lines)
    {
        ptr_line = buffer->lines->next_line;
        gui_line_free (buffer->lines);
        buffer->lines = ptr_line;
    }
    
    /* close log if opened */
    if (buffer->log_file)
        log_end (buffer);
    
    if (buffer->input_buffer)
        free (buffer->input_buffer);
    
    completion_free (&(buffer->completion));
    
    history_buffer_free (buffer);
    
    /* remove buffer from buffers list */
    if (buffer->prev_buffer)
        buffer->prev_buffer->next_buffer = buffer->next_buffer;
    if (buffer->next_buffer)
        buffer->next_buffer->prev_buffer = buffer->prev_buffer;
    if (gui_buffers == buffer)
        gui_buffers = buffer->next_buffer;
    if (last_gui_buffer == buffer)
        last_gui_buffer = buffer->prev_buffer;
    
    for (ptr_win = gui_windows; ptr_win; ptr_win = ptr_win->next_window)
    {
        if (ptr_win->buffer == buffer)
            ptr_win->buffer = NULL;
    }
    
    free (buffer);
    
    /* always at least one buffer */
    if (!gui_buffers && create_new && switch_to_another)
        (void) gui_buffer_new (gui_windows, NULL, NULL, 0, 1);
}

/*
 * gui_new_line: create new line for a buffer
 */

t_gui_line *
gui_new_line (t_gui_buffer *buffer)
{
    t_gui_line *new_line, *ptr_line;
    
    if ((new_line = (t_gui_line *) malloc (sizeof (struct t_gui_line))))
    {
        new_line->length = 0;
        new_line->length_align = 0;
        new_line->log_write = 1;
        new_line->line_with_message = 0;
        new_line->line_with_highlight = 0;
        new_line->messages = NULL;
        new_line->last_message = NULL;
        if (!buffer->lines)
            buffer->lines = new_line;
        else
            buffer->last_line->next_line = new_line;
        new_line->prev_line = buffer->last_line;
        new_line->next_line = NULL;
        buffer->last_line = new_line;
        buffer->num_lines++;
    }
    else
    {
        wee_log_printf (_("Not enough memory for new line\n"));
        return NULL;
    }
    
    /* remove one line if necessary */
    if ((cfg_history_max_lines > 0)
        && (buffer->num_lines > cfg_history_max_lines))
    {
        if (buffer->last_line == buffer->lines)
            buffer->last_line = NULL;
        ptr_line = buffer->lines->next_line;
        gui_line_free (buffer->lines);
        buffer->lines = ptr_line;
        ptr_line->prev_line = NULL;
        buffer->num_lines--;
        //if (buffer->first_line_displayed)
        gui_draw_buffer_chat (buffer, 1);
    }
    
    return new_line;
}

/*
 * gui_new_message: create a new message for last line of a buffer
 */

t_gui_message *
gui_new_message (t_gui_buffer *buffer)
{
    t_gui_message *new_message;
    
    if ((new_message = (t_gui_message *) malloc (sizeof (struct t_gui_message))))
    {
        if (!buffer->last_line->messages)
            buffer->last_line->messages = new_message;
        else
            buffer->last_line->last_message->next_message = new_message;
        new_message->prev_message = buffer->last_line->last_message;
        new_message->next_message = NULL;
        buffer->last_line->last_message = new_message;
    }
    else
    {
        wee_log_printf (_("Not enough memory for new message\n"));
        return NULL;
    }
    return new_message;
}

/*
 * gui_input_optimize_buffer_size: optimize input buffer size by adding
 *                                 or deleting data block (predefined size)
 */

void
gui_input_optimize_buffer_size (t_gui_buffer *buffer)
{
    int optimal_size;
    
    optimal_size = ((buffer->input_buffer_size / INPUT_BUFFER_BLOCK_SIZE) *
                   INPUT_BUFFER_BLOCK_SIZE) + INPUT_BUFFER_BLOCK_SIZE;
    if (buffer->input_buffer_alloc != optimal_size)
    {
        buffer->input_buffer_alloc = optimal_size;
        buffer->input_buffer = realloc (buffer->input_buffer, optimal_size);
    }
}

/*
 * gui_input_insert_string: insert a string into the input buffer
 */

void
gui_input_insert_string (char *string, int pos)
{
    int i, start, end, length;
    
    length = strlen (string);
    
    /* increase buffer size */
    gui_current_window->buffer->input_buffer_size += length;
    gui_input_optimize_buffer_size (gui_current_window->buffer);
    gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_size] = '\0';
    
    /* move end of string to the right */
    start = pos + length;
    end = gui_current_window->buffer->input_buffer_size - 1;
    for (i = end; i >= start; i--)
        gui_current_window->buffer->input_buffer[i] =
            gui_current_window->buffer->input_buffer[i - length];
    
    /* insert new string */
    strncpy (gui_current_window->buffer->input_buffer + pos, string, length);
}

/*
 * gui_input_insert_char: insert a char into input buffer
 */

void
gui_input_insert_char (int key)
{
    char new_char[3];
    t_irc_dcc *dcc_selected, *ptr_dcc, *ptr_dcc_next;
    
    if (key < 32)
        return;
    
    if (gui_current_window->buffer->dcc)
    {
        dcc_selected = (gui_current_window->dcc_selected) ?
            (t_irc_dcc *) gui_current_window->dcc_selected : dcc_list;
        switch (key)
        {
            /* accept DCC */
            case 'a':
            case 'A':
                if (dcc_selected
                    && (DCC_IS_RECV(dcc_selected->status))
                    && (dcc_selected->status == DCC_WAITING))
                {
                    dcc_accept (dcc_selected);
                }
                break;
            /* cancel DCC */
            case 'c':
            case 'C':
                if (dcc_selected
                    && (!DCC_ENDED(dcc_selected->status)))
                {
                    dcc_close (dcc_selected, DCC_ABORTED);
                    gui_redraw_buffer (gui_current_window->buffer);
                }
                break;
            /* purge old DCC */
            case 'p':
            case 'P':
                gui_current_window->dcc_selected = NULL;
                ptr_dcc = dcc_list;
                while (ptr_dcc)
                {
                    ptr_dcc_next = ptr_dcc->next_dcc;
                    if (DCC_ENDED(ptr_dcc->status))
                        dcc_free (ptr_dcc);
                    ptr_dcc = ptr_dcc_next;
                }
                gui_redraw_buffer (gui_current_window->buffer);
                break;
            /* close DCC window */
            case 'q':
            case 'Q':
                if (buffer_before_dcc)
                {
                    gui_buffer_free (gui_current_window->buffer, 1);
                    gui_switch_to_buffer (gui_current_window,
                                          buffer_before_dcc);
                }
                else
                    gui_buffer_free (gui_current_window->buffer, 1);
                gui_redraw_buffer (gui_current_window->buffer);
                break;
            /* remove from DCC list */
            case 'r':
            case 'R':
                if (dcc_selected
                    && (DCC_ENDED(dcc_selected->status)))
                {
                    if (dcc_selected->next_dcc)
                        gui_current_window->dcc_selected = dcc_selected->next_dcc;
                    else
                        gui_current_window->dcc_selected = NULL;
                    dcc_free (dcc_selected);
                    gui_redraw_buffer (gui_current_window->buffer);
                }
                break;
        }
    }
    else
    {
        /*gui_printf (gui_current_window->buffer,
          "[Debug] key pressed = %d, hex = %02X, octal = %o\n", key, key, key);*/
        new_char[0] = key;
        new_char[1] = '\0';
        
        gui_input_insert_string (new_char,
                                 gui_current_window->buffer->input_buffer_pos);
        gui_current_window->buffer->input_buffer_pos++;
        gui_draw_buffer_input (gui_current_window->buffer, 0);
        gui_current_window->buffer->completion.position = -1;
    }
}

/*
 * gui_input_return: terminate line (return pressed)
 */

void
gui_input_return ()
{
    t_gui_buffer *ptr_buffer;
    
    if (!gui_current_window->buffer->dcc)    
    {
        if (gui_current_window->buffer->input_buffer_size > 0)
        {
            gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_size] = '\0';
            history_add (gui_current_window->buffer, gui_current_window->buffer->input_buffer);
            gui_current_window->buffer->input_buffer_size = 0;
            gui_current_window->buffer->input_buffer_pos = 0;
            gui_current_window->buffer->input_buffer_1st_display = 0;
            gui_current_window->buffer->completion.position = -1;
            gui_current_window->buffer->ptr_history = NULL;
            ptr_buffer = gui_current_window->buffer;
            user_command (SERVER(gui_current_window->buffer),
                          gui_current_window->buffer,
                          gui_current_window->buffer->input_buffer);
            if (ptr_buffer == gui_current_window->buffer)
            {
                ptr_buffer->input_buffer[0] = '\0';
                gui_draw_buffer_input (ptr_buffer, 0);
            }
        }
    }
}

/*
 * gui_input_tab: tab key => completion
 */

void
gui_input_tab ()
{
    int i;

    if (!gui_current_window->buffer->dcc)
    {
        completion_search (&(gui_current_window->buffer->completion),
                           CHANNEL(gui_current_window->buffer),
                           gui_current_window->buffer->input_buffer,
                           gui_current_window->buffer->input_buffer_size,
                           gui_current_window->buffer->input_buffer_pos);
                    
        if (gui_current_window->buffer->completion.word_found)
        {
            /* replace word with new completed word into input buffer */
            if (gui_current_window->buffer->completion.diff_size > 0)
            {
                gui_current_window->buffer->input_buffer_size +=
                    gui_current_window->buffer->completion.diff_size;
                gui_input_optimize_buffer_size (gui_current_window->buffer);
                gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_size] = '\0';
                for (i = gui_current_window->buffer->input_buffer_size - 1;
                     i >=  gui_current_window->buffer->completion.position_replace +
                         (int)strlen (gui_current_window->buffer->completion.word_found); i--)
                    gui_current_window->buffer->input_buffer[i] =
                        gui_current_window->buffer->input_buffer[i -
                                                                 gui_current_window->buffer->completion.diff_size];
            }
            else
            {
                for (i = gui_current_window->buffer->completion.position_replace +
                         strlen (gui_current_window->buffer->completion.word_found);
                     i < gui_current_window->buffer->input_buffer_size; i++)
                    gui_current_window->buffer->input_buffer[i] =
                        gui_current_window->buffer->input_buffer[i -
                                                                 gui_current_window->buffer->completion.diff_size];
                gui_current_window->buffer->input_buffer_size +=
                    gui_current_window->buffer->completion.diff_size;
                gui_input_optimize_buffer_size (gui_current_window->buffer);
                gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_size] = '\0';
            }
                        
            strncpy (gui_current_window->buffer->input_buffer + gui_current_window->buffer->completion.position_replace,
                     gui_current_window->buffer->completion.word_found,
                     strlen (gui_current_window->buffer->completion.word_found));
            gui_current_window->buffer->input_buffer_pos =
                gui_current_window->buffer->completion.position_replace +
                strlen (gui_current_window->buffer->completion.word_found);
                        
            /* position is < 0 this means only one word was found to complete,
               so reinit to stop completion */
            if (gui_current_window->buffer->completion.position >= 0)
                gui_current_window->buffer->completion.position =
                    gui_current_window->buffer->input_buffer_pos;
                        
            /* add space or completor to the end of completion, if needed */
            if ((gui_current_window->buffer->completion.context == COMPLETION_COMMAND)
                || (gui_current_window->buffer->completion.context == COMPLETION_COMMAND_ARG))
            {
                if (gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_pos] != ' ')
                    gui_input_insert_string (" ",
                                             gui_current_window->buffer->input_buffer_pos);
                if (gui_current_window->buffer->completion.position >= 0)
                    gui_current_window->buffer->completion.position++;
                gui_current_window->buffer->input_buffer_pos++;
            }
            else
            {
                /* add nick completor if position 0 and completing nick */
                if ((gui_current_window->buffer->completion.base_word_pos == 0)
                    && (gui_current_window->buffer->completion.context == COMPLETION_NICK))
                {
                    if (strncmp (gui_current_window->buffer->input_buffer + gui_current_window->buffer->input_buffer_pos,
                                 cfg_look_completor, strlen (cfg_look_completor)) != 0)
                        gui_input_insert_string (cfg_look_completor,
                                                 gui_current_window->buffer->input_buffer_pos);
                    if (gui_current_window->buffer->completion.position >= 0)
                        gui_current_window->buffer->completion.position += strlen (cfg_look_completor);
                    gui_current_window->buffer->input_buffer_pos += strlen (cfg_look_completor);
                    if (gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_pos] != ' ')
                        gui_input_insert_string (" ",
                                                 gui_current_window->buffer->input_buffer_pos);
                    if (gui_current_window->buffer->completion.position >= 0)
                        gui_current_window->buffer->completion.position++;
                    gui_current_window->buffer->input_buffer_pos++;
                }
            }
            gui_draw_buffer_input (gui_current_window->buffer, 0);
        }
    }
}

/*
 * gui_input_backspace: backspace key
 */

void
gui_input_backspace ()
{
    int i;

    if (!gui_current_window->buffer->dcc)
    {
        if (gui_current_window->buffer->input_buffer_pos > 0)
        {
            i = gui_current_window->buffer->input_buffer_pos-1;
            while (gui_current_window->buffer->input_buffer[i])
            {
                gui_current_window->buffer->input_buffer[i] =
                    gui_current_window->buffer->input_buffer[i+1];
                i++;
            }
            gui_current_window->buffer->input_buffer_size--;
            gui_current_window->buffer->input_buffer_pos--;
            gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_size] = '\0';
            gui_draw_buffer_input (gui_current_window->buffer, 0);
            gui_input_optimize_buffer_size (gui_current_window->buffer);
            gui_current_window->buffer->completion.position = -1;
        }
    }
}

/*
 * gui_input_delete: delete key
 */

void
gui_input_delete ()
{
    int i;

    if (!gui_current_window->buffer->dcc)
    {
        if (gui_current_window->buffer->input_buffer_pos <
            gui_current_window->buffer->input_buffer_size)
        {
            i = gui_current_window->buffer->input_buffer_pos;
            while (gui_current_window->buffer->input_buffer[i])
            {
                gui_current_window->buffer->input_buffer[i] =
                    gui_current_window->buffer->input_buffer[i+1];
                i++;
            }
            gui_current_window->buffer->input_buffer_size--;
            gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_size] = '\0';
            gui_draw_buffer_input (gui_current_window->buffer, 0);
            gui_input_optimize_buffer_size (gui_current_window->buffer);
            gui_current_window->buffer->completion.position = -1;
        }
    }
}

/*
 * gui_input_delete_previous_word: delete previous word 
 */

void
gui_input_delete_previous_word ()
{
    int i, j, num_char_deleted, num_char_end;
    
    if (gui_current_window->buffer->input_buffer_pos > 0)
    {
        i = gui_current_window->buffer->input_buffer_pos - 1;
        while ((i >= 0) &&
            (gui_current_window->buffer->input_buffer[i] == ' '))
            i--;
        if (i >= 0)
        {
            while ((i >= 0) &&
                (gui_current_window->buffer->input_buffer[i] != ' '))
                i--;
            if (i >= 0)
            {
                while ((i >= 0) &&
                    (gui_current_window->buffer->input_buffer[i] == ' '))
                    i--;
            }
        }
        
        if (i >= 0)
            i++;
        i++;
        num_char_deleted = gui_current_window->buffer->input_buffer_pos - i;
        num_char_end = gui_current_window->buffer->input_buffer_size -
            gui_current_window->buffer->input_buffer_pos;
        
        for (j = 0; j < num_char_end; j++)
            gui_current_window->buffer->input_buffer[i + j] =
                gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_pos + j];
        
        gui_current_window->buffer->input_buffer_size -= num_char_deleted;
        gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_size] = '\0';
        gui_current_window->buffer->input_buffer_pos = i;
        gui_draw_buffer_input (gui_current_window->buffer, 0);
        gui_input_optimize_buffer_size (gui_current_window->buffer);
        gui_current_window->buffer->completion.position = -1;
    }
}

/*
 * gui_input_delete_next_word: delete next word 
 */

void
gui_input_delete_next_word ()
{
    int i, j, num_char_deleted;
    
    i = gui_current_window->buffer->input_buffer_pos;
    while (i < gui_current_window->buffer->input_buffer_size)
    {
        if ((gui_current_window->buffer->input_buffer[i] == ' ')
            && i != gui_current_window->buffer->input_buffer_pos)
            break;
        i++;
    }
    num_char_deleted = i - gui_current_window->buffer->input_buffer_pos;
    
    for (j = i; j < gui_current_window->buffer->input_buffer_size; j++)
        gui_current_window->buffer->input_buffer[j - num_char_deleted] =
            gui_current_window->buffer->input_buffer[j];
    
    gui_current_window->buffer->input_buffer_size -= num_char_deleted;
    gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_size] = '\0';
    gui_draw_buffer_input (gui_current_window->buffer, 0);
    gui_input_optimize_buffer_size (gui_current_window->buffer);
    gui_current_window->buffer->completion.position = -1;
}

/*
 * gui_input_delete_begin_of_line: delete all from cursor pos to beginning of line
 */

void
gui_input_delete_begin_of_line ()
{
    int i;
    
    for (i = gui_current_window->buffer->input_buffer_pos;
         i < gui_current_window->buffer->input_buffer_size; i++)
        gui_current_window->buffer->input_buffer[i - gui_current_window->buffer->input_buffer_pos] =
            gui_current_window->buffer->input_buffer[i];
    
    gui_current_window->buffer->input_buffer_size -=
        gui_current_window->buffer->input_buffer_pos;
    gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_size] = '\0';
    gui_current_window->buffer->input_buffer_pos = 0;
    gui_draw_buffer_input (gui_current_window->buffer, 0);
    gui_input_optimize_buffer_size (gui_current_window->buffer);
    gui_current_window->buffer->completion.position = -1;
}

/*
 * gui_input_delete_end_of_line: delete all from cursor pos to end of line
 */

void
gui_input_delete_end_of_line ()
{
    gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_pos] = ' ';
    gui_current_window->buffer->input_buffer_size = gui_current_window->buffer->input_buffer_pos ;
    gui_current_window->buffer->input_buffer[gui_current_window->buffer->input_buffer_size] = '\0';
    gui_draw_buffer_input (gui_current_window->buffer, 0);
    gui_input_optimize_buffer_size (gui_current_window->buffer);
    gui_current_window->buffer->completion.position = -1;
}

/*
 * gui_input_delete_line: delete entire line
 */

void
gui_input_delete_line ()
{
    gui_current_window->buffer->input_buffer[0] = '\0';
    gui_current_window->buffer->input_buffer_size = 0;
    gui_current_window->buffer->input_buffer_pos = 0;
    gui_draw_buffer_input (gui_current_window->buffer, 0);
    gui_input_optimize_buffer_size (gui_current_window->buffer);
    gui_current_window->buffer->completion.position = -1;
}

/*
 * gui_input_home: home key
 */

void
gui_input_home ()
{
    if (!gui_current_window->buffer->dcc)
    {
        if (gui_current_window->buffer->input_buffer_pos > 0)
        {
            gui_current_window->buffer->input_buffer_pos = 0;
            gui_draw_buffer_input (gui_current_window->buffer, 0);
        }
    }
}

/*
 * gui_input_end: end key
 */

void
gui_input_end ()
{
    if (!gui_current_window->buffer->dcc)
    {
        if (gui_current_window->buffer->input_buffer_pos <
            gui_current_window->buffer->input_buffer_size)
        {
            gui_current_window->buffer->input_buffer_pos =
                gui_current_window->buffer->input_buffer_size;
            gui_draw_buffer_input (gui_current_window->buffer, 0);
        }
    }
}

/*
 * gui_input_left: move to previous char
 */

void
gui_input_left ()
{
    if (!gui_current_window->buffer->dcc)
    {
        if (gui_current_window->buffer->input_buffer_pos > 0)
        {
            gui_current_window->buffer->input_buffer_pos--;
            gui_draw_buffer_input (gui_current_window->buffer, 0);
        }
    }
}

/*
 * gui_input_previous_word: move to beginning of previous word
 */

void
gui_input_previous_word ()
{
    int i;
    
    if (gui_current_window->buffer->input_buffer_pos > 0)
    {
        i = gui_current_window->buffer->input_buffer_pos - 1;
        while ((i >= 0) &&
            (gui_current_window->buffer->input_buffer[i] == ' '))
            i--;
        if (i < 0)
            gui_current_window->buffer->input_buffer_pos = 0;
        else
        {
            while ((i >= 0) &&
                (gui_current_window->buffer->input_buffer[i] != ' '))
                i--;
            gui_current_window->buffer->input_buffer_pos = i + 1;
        }
        gui_draw_buffer_input (gui_current_window->buffer, 0);
    }
}

/*
 * gui_input_right: move to previous char
 */

void
gui_input_right ()
{
    if (!gui_current_window->buffer->dcc)
    {
        if (gui_current_window->buffer->input_buffer_pos <
            gui_current_window->buffer->input_buffer_size)
        {
            gui_current_window->buffer->input_buffer_pos++;
            gui_draw_buffer_input (gui_current_window->buffer, 0);
        }
    }
}

/*
 * gui_input_next_word: move to the end of next
 */

void
gui_input_next_word ()
{
    int i;
    
    if (gui_current_window->buffer->input_buffer_pos <
        gui_current_window->buffer->input_buffer_size + 1)
    {
        i = gui_current_window->buffer->input_buffer_pos;
        while ((i <= gui_current_window->buffer->input_buffer_size) &&
            (gui_current_window->buffer->input_buffer[i] == ' '))
            i++;
        if (i > gui_current_window->buffer->input_buffer_size)
            gui_current_window->buffer->input_buffer_pos = i - 1;
        else
        {
            while ((i <= gui_current_window->buffer->input_buffer_size) &&
                (gui_current_window->buffer->input_buffer[i] != ' '))
                i++;
            if (i > gui_current_window->buffer->input_buffer_size)
                gui_current_window->buffer->input_buffer_pos =
                    gui_current_window->buffer->input_buffer_size;
            else
                gui_current_window->buffer->input_buffer_pos = i;
            
        }
        gui_draw_buffer_input (gui_current_window->buffer, 0);
    }
}

/*
 * gui_input_up: recall last command or move to previous DCC in list
 */

void
gui_input_up ()
{
    if (gui_current_window->buffer->dcc)
    {
        if (dcc_list)
        {
            if (gui_current_window->dcc_selected
                && ((t_irc_dcc *)(gui_current_window->dcc_selected))->prev_dcc)
            {
                if (gui_current_window->dcc_selected ==
                    gui_current_window->dcc_first)
                    gui_current_window->dcc_first =
                        ((t_irc_dcc *)(gui_current_window->dcc_first))->prev_dcc;
                gui_current_window->dcc_selected =
                    ((t_irc_dcc *)(gui_current_window->dcc_selected))->prev_dcc;
                gui_draw_buffer_chat (gui_current_window->buffer, 1);
                gui_draw_buffer_input (gui_current_window->buffer, 1);
            }
        }
    }
    else
    {
        if (gui_current_window->buffer->ptr_history)
        {
            gui_current_window->buffer->ptr_history =
                gui_current_window->buffer->ptr_history->next_history;
            if (!gui_current_window->buffer->ptr_history)
                gui_current_window->buffer->ptr_history =
                    gui_current_window->buffer->history;
        }
        else
            gui_current_window->buffer->ptr_history =
                gui_current_window->buffer->history;
        if (gui_current_window->buffer->ptr_history)
        {
            gui_current_window->buffer->input_buffer_size =
                strlen (gui_current_window->buffer->ptr_history->text);
            gui_input_optimize_buffer_size (gui_current_window->buffer);
            gui_current_window->buffer->input_buffer_pos =
                gui_current_window->buffer->input_buffer_size;
            strcpy (gui_current_window->buffer->input_buffer,
                    gui_current_window->buffer->ptr_history->text);
            gui_draw_buffer_input (gui_current_window->buffer, 0);
        }
    }
}

/*
 * gui_input_down: recall next command or move to next DCC in list
 */

void
gui_input_down ()
{
    if (gui_current_window->buffer->dcc)
    {
        if (dcc_list)
        {
            if (!gui_current_window->dcc_selected
                || ((t_irc_dcc *)(gui_current_window->dcc_selected))->next_dcc)
            {
                if (gui_current_window->dcc_last_displayed
                    && (gui_current_window->dcc_selected ==
                        gui_current_window->dcc_last_displayed))
                {
                    if (gui_current_window->dcc_first)
                        gui_current_window->dcc_first =
                            ((t_irc_dcc *)(gui_current_window->dcc_first))->next_dcc;
                    else
                        gui_current_window->dcc_first =
                            dcc_list->next_dcc;
                }
                if (gui_current_window->dcc_selected)
                    gui_current_window->dcc_selected =
                        ((t_irc_dcc *)(gui_current_window->dcc_selected))->next_dcc;
                else
                    gui_current_window->dcc_selected =
                        dcc_list->next_dcc;
                gui_draw_buffer_chat (gui_current_window->buffer, 1);
                gui_draw_buffer_input (gui_current_window->buffer, 1);
            }
        }
    }
    else
    {
        if (gui_current_window->buffer->ptr_history)
        {
            gui_current_window->buffer->ptr_history =
                gui_current_window->buffer->ptr_history->prev_history;
            if (gui_current_window->buffer->ptr_history)
                gui_current_window->buffer->input_buffer_size =
                    strlen (gui_current_window->buffer->ptr_history->text);
            else
                gui_current_window->buffer->input_buffer_size = 0;
            gui_input_optimize_buffer_size (gui_current_window->buffer);
            gui_current_window->buffer->input_buffer_pos =
                gui_current_window->buffer->input_buffer_size;
            if (gui_current_window->buffer->ptr_history)
                strcpy (gui_current_window->buffer->input_buffer,
                        gui_current_window->buffer->ptr_history->text);
            gui_draw_buffer_input (gui_current_window->buffer, 0);
        }
    }
}

/*
 * gui_input_jump_smart: jump to buffer with activity (alt-A by default)
 */

void
gui_input_jump_smart ()
{
    if (hotlist)
    {
        if (!hotlist_initial_buffer)
            hotlist_initial_buffer = gui_current_window->buffer;
        gui_switch_to_buffer (gui_current_window, hotlist->buffer);
        gui_redraw_buffer (gui_current_window->buffer);
    }
    else
    {
        if (hotlist_initial_buffer)
        {
            gui_switch_to_buffer (gui_current_window, hotlist_initial_buffer);
            gui_redraw_buffer (gui_current_window->buffer);
            hotlist_initial_buffer = NULL;
        }
    }
}

/*
 * gui_input_jump_dcc: jump to DCC buffer
 */

void
gui_input_jump_dcc ()
{
    if (gui_current_window->buffer->dcc)
    {
        if (buffer_before_dcc)
        {
            gui_switch_to_buffer (gui_current_window,
                                  buffer_before_dcc);
            gui_redraw_buffer (gui_current_window->buffer);
        }
    }
    else
    {
        buffer_before_dcc = gui_current_window->buffer;
        gui_switch_to_dcc_buffer ();
    }
}

/*
 * gui_input_jump_server: jump to server buffer
 */

void
gui_input_jump_server ()
{
    if (!gui_current_window->buffer->dcc)
    {
        if (SERVER(gui_current_window->buffer)->buffer !=
            gui_current_window->buffer)
        {
            gui_switch_to_buffer (gui_current_window,
                                  SERVER(gui_current_window->buffer)->buffer);
            gui_redraw_buffer (gui_current_window->buffer);
        }
    }
}

/*
 * gui_input_jump_next_server: jump to next server
 */

void
gui_input_jump_next_server ()
{
    t_irc_server *ptr_server;
    t_gui_buffer *ptr_buffer;
    
    if (!gui_current_window->buffer->dcc)
    {
        ptr_server = SERVER(gui_current_window->buffer)->next_server;
        if (!ptr_server)
            ptr_server = irc_servers;
        while (ptr_server != SERVER(gui_current_window->buffer))
        {
            if (ptr_server->buffer)
                break;
            ptr_server = (ptr_server->next_server) ?
                ptr_server->next_server : irc_servers;
        }
        if (ptr_server != SERVER(gui_current_window->buffer))
        {
            ptr_buffer = (ptr_server->channels) ?
                ptr_server->channels->buffer : ptr_server->buffer;
            gui_switch_to_buffer (gui_current_window, ptr_buffer);
            gui_redraw_buffer (gui_current_window->buffer);
        }
    }
}

/*
 * gui_input_hotlist_clear: clear hotlist
 */

void
gui_input_hotlist_clear ()
{
    if (hotlist)
    {
        hotlist_free_all ();
        gui_redraw_buffer (gui_current_window->buffer);
    }
    hotlist_initial_buffer = gui_current_window->buffer;
}

/*
 * gui_input_infobar_clear: clear infobar
 */

void
gui_input_infobar_clear ()
{
    gui_infobar_remove ();
    gui_draw_buffer_infobar (gui_current_window->buffer, 1);
}

/*
 * gui_switch_to_previous_buffer: switch to previous buffer
 */

void
gui_switch_to_previous_buffer ()
{
    if (!gui_ok)
        return;
    
    /* if only one buffer then return */
    if (gui_buffers == last_gui_buffer)
        return;
    
    if (gui_current_window->buffer->prev_buffer)
        gui_switch_to_buffer (gui_current_window, gui_current_window->buffer->prev_buffer);
    else
        gui_switch_to_buffer (gui_current_window, last_gui_buffer);
    
    gui_redraw_buffer (gui_current_window->buffer);
}

/*
 * gui_switch_to_next_buffer: switch to next buffer
 */

void
gui_switch_to_next_buffer ()
{
    if (!gui_ok)
        return;
    
    /* if only one buffer then return */
    if (gui_buffers == last_gui_buffer)
        return;
    
    if (gui_current_window->buffer->next_buffer)
        gui_switch_to_buffer (gui_current_window, gui_current_window->buffer->next_buffer);
    else
        gui_switch_to_buffer (gui_current_window, gui_buffers);
    
    gui_redraw_buffer (gui_current_window->buffer);
}

/*
 * gui_switch_to_previous_window: switch to previous window
 */

void
gui_switch_to_previous_window ()
{
    if (!gui_ok)
        return;
    
    /* if only one window then return */
    if (gui_windows == last_gui_window)
        return;
    
    gui_current_window = (gui_current_window->prev_window) ? gui_current_window->prev_window : last_gui_window;
    gui_switch_to_buffer (gui_current_window, gui_current_window->buffer);
    gui_redraw_buffer (gui_current_window->buffer);
}

/*
 * gui_switch_to_next_window: switch to next window
 */

void
gui_switch_to_next_window ()
{
    if (!gui_ok)
        return;
    
    /* if only one window then return */
    if (gui_windows == last_gui_window)
        return;
    
    gui_current_window = (gui_current_window->next_window) ? gui_current_window->next_window : gui_windows;
    gui_switch_to_buffer (gui_current_window, gui_current_window->buffer);
    gui_redraw_buffer (gui_current_window->buffer);
}

/*
 * gui_switch_to_buffer_by_number: switch to another buffer with number
 */

t_gui_buffer *
gui_switch_to_buffer_by_number (t_gui_window *window, int number)
{
    t_gui_buffer *ptr_buffer;
    
    /* invalid buffer */
    if (number < 0)
        return NULL;
    
    /* buffer is currently displayed ? */
    if (number == window->buffer->number)
        return window->buffer;
    
    /* search for buffer in the list */
    for (ptr_buffer = gui_buffers; ptr_buffer; ptr_buffer = ptr_buffer->next_buffer)
    {
        if ((ptr_buffer != window->buffer) && (number == ptr_buffer->number))
        {
            gui_switch_to_buffer (window, ptr_buffer);
            gui_redraw_buffer (window->buffer);
            return ptr_buffer;
        }
    }
    
    /* buffer not found */
    return NULL;
}

/*
 * gui_switch_to_buffer_by_number: switch to another buffer with number
 */

void
gui_move_buffer_to_number (t_gui_window *window, int number)
{
    t_gui_buffer *ptr_buffer;
    int i;
    
    /* buffer number is already ok ? */
    if (number == window->buffer->number)
        return;
    
    if (number < 1)
        number = 1;
    
    /* remove buffer from list */
    if (window->buffer == gui_buffers)
    {
        gui_buffers = window->buffer->next_buffer;
        gui_buffers->prev_buffer = NULL;
    }
    if (window->buffer == last_gui_buffer)
    {
        last_gui_buffer = window->buffer->prev_buffer;
        last_gui_buffer->next_buffer = NULL;
    }
    if (window->buffer->prev_buffer)
        (window->buffer->prev_buffer)->next_buffer = window->buffer->next_buffer;
    if (window->buffer->next_buffer)
        (window->buffer->next_buffer)->prev_buffer = window->buffer->prev_buffer;
    
    if (number == 1)
    {
        gui_buffers->prev_buffer = window->buffer;
        window->buffer->prev_buffer = NULL;
        window->buffer->next_buffer = gui_buffers;
        gui_buffers = window->buffer;
    }
    else
    {
        /* assign new number to all buffers */
        i = 1;
        for (ptr_buffer = gui_buffers; ptr_buffer; ptr_buffer = ptr_buffer->next_buffer)
        {
            ptr_buffer->number = i++;
        }
        
        /* search for new position in the list */
        for (ptr_buffer = gui_buffers; ptr_buffer; ptr_buffer = ptr_buffer->next_buffer)
        {
            if (ptr_buffer->number == number)
                break;
        }
        if (ptr_buffer)
        {
            /* insert before buffer found */
            window->buffer->prev_buffer = ptr_buffer->prev_buffer;
            window->buffer->next_buffer = ptr_buffer;
            if (ptr_buffer->prev_buffer)
                (ptr_buffer->prev_buffer)->next_buffer = window->buffer;
            ptr_buffer->prev_buffer = window->buffer;
        }
        else
        {
            /* number not found (too big)? => add to end */
            window->buffer->prev_buffer = last_gui_buffer;
            window->buffer->next_buffer = NULL;
            last_gui_buffer->next_buffer = window->buffer;
            last_gui_buffer = window->buffer;
        }
        
    }
    
    /* assign new number to all buffers */
    i = 1;
    for (ptr_buffer = gui_buffers; ptr_buffer; ptr_buffer = ptr_buffer->next_buffer)
    {
        ptr_buffer->number = i++;
    }
    
    gui_redraw_buffer (window->buffer);
}

/*
 * gui_window_print_log: print window infos in log (usually for crash dump)
 */

void
gui_window_print_log (t_gui_window *window)
{
    wee_log_printf ("[window (addr:0x%X)]\n", window);
    wee_log_printf ("  win_x . . . . . . . : %d\n",   window->win_x);
    wee_log_printf ("  win_y . . . . . . . : %d\n",   window->win_y);
    wee_log_printf ("  win_width . . . . . : %d\n",   window->win_width);
    wee_log_printf ("  win_height. . . . . : %d\n",   window->win_height);
    wee_log_printf ("  win_chat_x. . . . . : %d\n",   window->win_chat_x);
    wee_log_printf ("  win_chat_y. . . . . : %d\n",   window->win_chat_y);
    wee_log_printf ("  win_chat_width. . . : %d\n",   window->win_chat_width);
    wee_log_printf ("  win_chat_height . . : %d\n",   window->win_chat_height);
    wee_log_printf ("  win_chat_cursor_x . : %d\n",   window->win_chat_cursor_x);
    wee_log_printf ("  win_chat_cursor_y . : %d\n",   window->win_chat_cursor_y);
    wee_log_printf ("  win_nick_x. . . . . : %d\n",   window->win_nick_x);
    wee_log_printf ("  win_nick_y. . . . . : %d\n",   window->win_nick_y);
    wee_log_printf ("  win_nick_width. . . : %d\n",   window->win_nick_width);
    wee_log_printf ("  win_nick_height . . : %d\n",   window->win_nick_height);
    wee_log_printf ("  win_nick_start. . . : %d\n",   window->win_nick_start);
    wee_log_printf ("  win_title . . . . . : 0x%X\n", window->win_title);
    wee_log_printf ("  win_chat. . . . . . : 0x%X\n", window->win_chat);
    wee_log_printf ("  win_nick. . . . . . : 0x%X\n", window->win_nick);
    wee_log_printf ("  win_status. . . . . : 0x%X\n", window->win_status);
    wee_log_printf ("  win_infobar . . . . : 0x%X\n", window->win_infobar);
    wee_log_printf ("  win_input . . . . . : 0x%X\n", window->win_input);
    wee_log_printf ("  win_separator . . . : 0x%X\n", window->win_separator);
    wee_log_printf ("  textview_chat . . . : 0x%X\n", window->textview_chat);
    wee_log_printf ("  textbuffer_chat . . : 0x%X\n", window->textbuffer_chat);
    wee_log_printf ("  texttag_chat. . . . : 0x%X\n", window->texttag_chat);
    wee_log_printf ("  textview_nicklist . : 0x%X\n", window->textview_nicklist);
    wee_log_printf ("  textbuffer_nicklist : 0x%X\n", window->textbuffer_nicklist);
    wee_log_printf ("  dcc_first . . . . . : 0x%X\n", window->dcc_first);
    wee_log_printf ("  dcc_selected. . . . : 0x%X\n", window->dcc_selected);
    wee_log_printf ("  dcc_last_displayed. : 0x%X\n", window->dcc_last_displayed);
    wee_log_printf ("  buffer. . . . . . . : 0x%X\n", window->buffer);
    wee_log_printf ("  first_line_displayed: %d\n",   window->first_line_displayed);
    wee_log_printf ("  sub_lines . . . . . : %d\n",   window->sub_lines);
    wee_log_printf ("  prev_window . . . . : 0x%X\n", window->prev_window);
    wee_log_printf ("  next_window . . . . : 0x%X\n", window->next_window);
    
}

/*
 * gui_buffer_print_log: print buffer infos in log (usually for crash dump)
 */

void
gui_buffer_print_log (t_gui_buffer *buffer)
{
    t_gui_line *ptr_line;
    t_gui_message *ptr_message;
    int num;
    char buf[4096];
    
    wee_log_printf ("[buffer (addr:0x%X)]\n", buffer);
    wee_log_printf ("  num_displayed. . . . : %d\n",   buffer->num_displayed);
    wee_log_printf ("  number . . . . . . . : %d\n",   buffer->number);
    wee_log_printf ("  server . . . . . . . : 0x%X\n", buffer->server);
    wee_log_printf ("  channel. . . . . . . : 0x%X\n", buffer->channel);
    wee_log_printf ("  dcc. . . . . . . . . : %d\n",   buffer->dcc);
    wee_log_printf ("  lines. . . . . . . . : 0x%X\n", buffer->lines);
    wee_log_printf ("  last_line. . . . . . : 0x%X\n", buffer->last_line);
    wee_log_printf ("  num_lines. . . . . . : %d\n",   buffer->num_lines);
    wee_log_printf ("  line_complete. . . . : %d\n",   buffer->line_complete);
    wee_log_printf ("  notify_level . . . . : %d\n",   buffer->notify_level);
    wee_log_printf ("  log_filename . . . . : '%s'\n", buffer->log_filename);
    wee_log_printf ("  log_file . . . . . . : 0x%X\n", buffer->log_file);
    wee_log_printf ("  input_buffer . . . . : '%s'\n", buffer->input_buffer);
    wee_log_printf ("  input_buffer_alloc . : %d\n",   buffer->input_buffer_alloc);
    wee_log_printf ("  input_buffer_size. . : %d\n",   buffer->input_buffer_size);
    wee_log_printf ("  input_buffer_pos . . : %d\n",   buffer->input_buffer_pos);
    wee_log_printf ("  input_buffer_1st_disp: %d\n",   buffer->input_buffer_1st_display);
    wee_log_printf ("  history. . . . . . . : 0x%X\n", buffer->history);
    wee_log_printf ("  last_history . . . . : 0x%X\n", buffer->last_history);
    wee_log_printf ("  ptr_history. . . . . : 0x%X\n", buffer->ptr_history);
    wee_log_printf ("  prev_buffer. . . . . : 0x%X\n", buffer->prev_buffer);
    wee_log_printf ("  next_buffer. . . . . : 0x%X\n", buffer->next_buffer);
    wee_log_printf ("\n");
    wee_log_printf ("  => last 100 lines:\n");
    
    num = 0;
    ptr_line = buffer->last_line;
    while (ptr_line && (num < 100))
    {
        num++;
        ptr_line = ptr_line->prev_line;
    }
    if (!ptr_line)
        ptr_line = buffer->lines;
    else
        ptr_line = ptr_line->next_line;
    
    while (ptr_line)
    {
        buf[0] = '\0';
        for (ptr_message = ptr_line->messages; ptr_message;
            ptr_message = ptr_message->next_message)
        {
            if (strlen (buf) + strlen (ptr_message->message) + 1 >= sizeof (buf))
                break;
            strcat (buf, ptr_message->message);
        }
        num--;
        wee_log_printf ("       line N-%05d: %s\n", num, buf);
        
        ptr_line = ptr_line->next_line;
    }
}
