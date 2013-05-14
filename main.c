/******************************************************************************
 * main.c
 *
 * treetop - A 'top' like text/log file monitor.
 *
 * Copyright (C) 2013, Matt Davis (enferex)
 *
 * This file is part of treetop.
 * treetop is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * treetop is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with treetop.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <unistd.h>
#include <curses.h>
#include <menu.h>
#include <panel.h>
#include <sys/stat.h>


/* Output routines */
#define _PR(_tag, _outfd, ...)                       \
    do {                                             \
        fprintf(_outfd, "[logtop]"_tag __VA_ARGS__); \
        fputc('\n', _outfd);                         \
    } while(0)
#define PR(...)  _PR(" ", stdout, __VA_ARGS__)
#define DBG(...) _PR("[debug] ", stdout, __VA_ARGS__)
#define WR(...)  _PR("[warning] ", stderr, __VA_ARGS__)
#define ER(...)                               \
    do {                                      \
        _PR("[error] ", stderr, __VA_ARGS__); \
        exit(-1);                             \
    } while (0)


/* Comment character for config file (anything after this char is ignored) */
#define COMMENT_CHAR '#'


/* If the file has the 'UPDATED' state */
#define UPDATED_CHAR "*"


/* Delay milliseconds */
#define DELAY_MS 5000


/* Max */
#define MAX(_a, _b) (((_a)>(_b)) ? (_a) : (_b))


/* File state */
typedef enum _state_e
{
    UNCHANGED,
    UPDATED
} state_e;


/* File information */
typedef struct _data_t
{
    int fd;
    FILE *fp;
    const char *full_path;
    const char *base_name;
    char line[128];
    struct _data_t *next;
    state_e state;
    ITEM *item;  /* Curses menu item for this file */
    time_t last_mod;
} data_t;


/* Screen (ncurses state and content) */
typedef struct _screen_t
{
    WINDOW *master;  /* Nothing here it just needs a border to look pretty */
    WINDOW *content; /* Menu goes here                                     */
    WINDOW *details; /* Display details about selected item                */
    PANEL  *master_panel;
    PANEL  *content_panel;
    PANEL  *details_panel;
    MENU *menu;
    ITEM **items;
    data_t *datas;
} screen_t;


static void usage(const char *execname)
{
    PR("Usage: %s <config>\n", execname);
    exit(0);
}


/* Update display */
static void screen_create_menu(screen_t *screen)
{
    int i;
    data_t *d;
    char line[COLS];
    const char *def = "Updating...";

    /* Count number of data items */
    for (d=screen->datas; d; d=d->next)
      ++i;
    
    /* Allocate a long line for the description (make it all spaces) */ 
    memset(line, ' ', sizeof(line) - 1);
    line[sizeof(line)] = '\0';
    memcpy(line, def, strlen(def));

    /* Allocate and create menu items (one per data item */
    screen->items = (ITEM **)calloc(i+1, sizeof(ITEM *));
    for (i=0, d=screen->datas; d; d=d->next, ++i)
    {
        screen->items[i] = new_item(d->base_name, line);
        screen->items[i]->description.length = sizeof(line);
        set_item_userptr(screen->items[i], (void *)d);
        d->item = screen->items[i];
    }

    screen->menu = new_menu(screen->items);
    set_menu_mark(screen->menu, "-->  ");
    set_menu_win(screen->menu, screen->content);
    post_menu(screen->menu);
}


/* Returns the starting x-coordinate such that when displaying a value of
 * 'length' characters long, it will be centered in the given window.
 */
static int find_center_start(const WINDOW *win, size_t length)
{
    int max_x = getmaxx(win);
    int half = length / 2;
    return max_x / 2 - half;
}


/* Initialize curses */
static screen_t *screen_create(data_t *datas)
{
    int x;
    const char *title = "}-= TreeTop =-{";
    screen_t *screen;

    initscr();
    cbreak();
    noecho();
    curs_set(0); /* Turn cursor off */
    keypad(stdscr, TRUE);
    timeout(DELAY_MS);

    screen = calloc(1, sizeof(screen_t));
    screen->datas = datas;

    /* Create the windows */
    screen->master = newwin(LINES, COLS, 0, 0);
    screen->content = newwin(LINES-4, COLS-4, 2, 2);
    screen->details = newwin(LINES-8, COLS-8, 2, 2);

    /* Decorate the master window */
    box(screen->master, 0, 0);
    x = find_center_start(screen->master, strlen(title));
    mvwprintw(screen->master, 0, x, title);

    /* Put the windows in panels (easier to refresh things) */
    screen->master_panel = new_panel(screen->master);
    screen->details_panel = new_panel(screen->details);
    screen->content_panel = new_panel(screen->content);
    screen_create_menu(screen);
    return screen;
}


/* Cleanup from curses */
static void screen_destroy(screen_t *screen)
{
    endwin();
    free(screen);
}


/* Create our file information */
static data_t *data_init(const char *fname)
{
    FILE *fp, *entry_fp;
    data_t *head, *tmp;
    char *c, *line;
    size_t sz;
    ssize_t ret;
#define CONTINUE {free(line); line=NULL; continue;}

    if (!(fp = fopen(fname, "r")))
      ER("Could not open config file '%s'", fname);

    /* For each line in config */
    head = NULL;
    line = NULL;
    while ((ret = getline(&line, &sz, fp)) != -1)
    {
        /* Skip whitespace */
        c = line;
        while (*c && isspace(*c))
          ++c;

        if (*c == COMMENT_CHAR)
          CONTINUE;

        /* Trim line */
        if (strchr(c, COMMENT_CHAR))
          *(strchr(c, COMMENT_CHAR)) = '\0';
        if (strchr(c, '\n'))
          *(strchr(c, '\n')) = '\0';
        if (strchr(c, ' '))
          *(strchr(c, ' ')) = '\0';

        if (strlen(c) == 0)
          CONTINUE;

        if (!(entry_fp = fopen(c, "r")))
        {
            WR("Could not open file: '%s'", c);
            CONTINUE;
        }

        /* Add to list */ 
        DBG("Monitoring file: '%s'...", c);
        tmp = head;
        head = calloc(1, sizeof(data_t));
        head->fp = entry_fp;
        head->fd = fileno(entry_fp);
        head->full_path = strdup(c);
        head->base_name = strdup(basename((char *)head->full_path));
        head->state = UPDATED; /* Force first update to process this */
        head->next = tmp;
        free(line);
        line = NULL;
    }

    return head;
}


/* Cleanup */
static void data_destroy(data_t *datas)
{
    data_t *curr, *d = datas;

    while (d)
    {
        curr = d;
        fclose(d->fp);
        d = curr->next;
        free(curr);
    }
}


/* Set the last line for this file */
static void get_last_line(data_t *d)
{
    ssize_t idx, n_bytes;
    char line[1024] = {0};

    /* Read in last 1024 bytes */
    if (fseek(d->fp, -sizeof(line), SEEK_END) == -1)
      fseek(d->fp, 0, SEEK_SET);
    n_bytes = fread(line, 1, sizeof(line)-1, d->fp);
    idx = n_bytes - 1;

    /* If file ends with newline */
    while (idx > 0 && (line[idx] == '\n' || line[idx] == '\r'))
      --idx;

    /* Now find the next newline */
    while (idx > 0 && (line[idx] != '\n' && line[idx] != '\r'))
      --idx;

    if (line[idx] == '\n' && idx+1 < n_bytes)
      ++idx;

    /* Copy starting from the last line */
    if (idx >= 0)
      strncpy(d->line, line+idx, sizeof(d->line) - 1);
}


/* Update data */
static void data_update(data_t *datas)
{
    data_t *d;
    struct stat stat;

    /* If the files have been updated, grab the last line from the file */
    for (d=datas; d; d=d->next)
    {
        if (fstat(d->fd, &stat) == -1)
          ER("Could not obtain file stats for: '%s'", d->base_name);

        /* If the file has been modified since last check, update */
        if (stat.st_mtime != d->last_mod)
        {
            get_last_line(d);
            d->last_mod = stat.st_mtime;
            d->state = UPDATED;
        }
    }
}


/* Update the details screen to display info about the selected item */
static void update_details(screen_t *screen, const data_t *selected)
{
    int maxx, maxy, x, y, d_idx, b_idx;
    char buf[1024]={0}, disp[1024]={0};

    wclear(screen->details);

    /* Read in buffer */
    if (fseek(selected->fp, -sizeof(buf), SEEK_END) == -1)
      fseek(selected->fp, 0, SEEK_SET);
    fread(buf, sizeof(buf), 1, selected->fp);
    buf[sizeof(buf)-1] = '\0';
    
    /* Draw last n bytes of file: (figure out how much room we have) */
    getmaxyx(screen->details, maxy, maxx);
    maxy -= 2; /* Padding between file data and filename */
    --maxx;

    while ((maxx * maxy) > sizeof(buf))
      --maxy;

    /* Wrap */
    b_idx = d_idx = 0;
    for (y=0; y<maxy; ++y)
    {
        disp[d_idx++] = ' ';
        for (x=0; x<maxx-1; ++x)
        {
            if (buf[b_idx]=='\n' || buf[b_idx] == '\r')
            {
                ++b_idx;
                break;
            }
            disp[d_idx++] = buf[b_idx++];
        }
        disp[d_idx++] = '\n';
    }

    disp[d_idx] = '\0';
    mvwprintw(screen->details, 2, 0, disp);
    
    /* Display file name and draw border */
    mvwprintw(screen->details, 1, 1, "%s:\n", selected->base_name);
    box(screen->details, 0, 0);
}


/* Update display
 * 'show_details' is the selected item, if no item is slected this val is NULL
 */
static void screen_update(screen_t *screen, const data_t *show_details)
{
    data_t *d;

    /* Check for updated data */
    for (d=screen->datas; d; d=d->next)
      if (d->state == UPDATED)
        d->item->description.str = d->line;

    /* Refresh menu */
    unpost_menu(screen->menu);
    post_menu(screen->menu);

    /* Now draw a character signifying which file just changed */
    for (d=screen->datas; d; d=d->next)
    {
        if (d->state == UPDATED)
        {
            mvwprintw(screen->content, item_index(d->item), 3, UPDATED_CHAR);
            d->state = UNCHANGED;
        }
    }

    /* Update display */
    if (show_details)
    {
        update_details(screen, show_details);
        show_panel(screen->details_panel);
    }
    else
      hide_panel(screen->details_panel);
       
    update_panels(); 
    doupdate();
}


/* Capture user input (keys) and timeout to periodically referesh */
static void process(screen_t *screen)
{
    int c, do_update;
    const data_t *show_details;
    
    /* Force initial drawing */    
    data_update(screen->datas);
    screen_update(screen, NULL);

    while ((c = getch()) != 'Q' && c != 'q')
    {
        do_update = (c == -1) ? 0 : 1;
        show_details = NULL;
        switch (c)
        {
            case KEY_UP:
                menu_driver(screen->menu, REQ_UP_ITEM);
                break;
            case KEY_DOWN:
                menu_driver(screen->menu, REQ_DOWN_ITEM);
                break;
            case KEY_LEFT:
                show_details = item_userptr(current_item(screen->menu));
                break;
            default:
                break;
        }

        if (do_update)
        {
            data_update(screen->datas);
            screen_update(screen, show_details);
        }
    }
}


int main(int argc, char **argv)
{
    screen_t *screen;
    data_t *datas;
    const char *fname;

    /* Args */
    if (argc != 2)
      usage(argv[0]);
    fname = argv[1];
    DBG("Using config: %s\n", fname);

    /* Load data */
    datas = data_init(fname);

    /* Initialize display */
    screen = screen_create(datas);

    /* Do the work */
    process(screen);

    /* Cleanup */
    data_destroy(datas);
    screen_destroy(screen);

    return 0;
}
