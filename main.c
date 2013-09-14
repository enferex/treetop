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
#include <sys/types.h>


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


/* Default delay (seconds) */
#define DEFAULT_TIMEOUT_SECS 10


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


static void usage(const char *execname, const char *msg)
{
    if (msg)
      PR("%s", msg);
    printf("Usage: %s <config> [-d secs] [-h]\n"
       "    -h:      Display this help screen\n"
       "    -d secs: Auto-update display every 'secs' seconds\n", execname);
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
static screen_t *screen_create(data_t *datas, int timeout_ms)
{
    int x;
    const char *title = "}-= TreeTop =-{";
    screen_t *screen;

    initscr();
    cbreak();
    noecho();
    curs_set(0); /* Turn cursor off */
    timeout(timeout_ms);
    keypad(stdscr, TRUE);

    screen = calloc(1, sizeof(screen_t));
    screen->datas = datas;

    /* Create the windows */
    screen->master = newwin(LINES, COLS, 0, 0);
    screen->content = newwin(LINES-3, COLS-2, 2, 1);
    screen->details = newwin(LINES-3, COLS-2, 2, 1);
    scrollok(screen->details, TRUE);

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
    struct stat stats;

    /* If the files have been updated, grab the last line from the file */
    for (d=datas; d; d=d->next)
    {
        if (stat(d->full_path, &stats) == -1)
          ER("Could not obtain file stats for: '%s'", d->base_name);

        /* If the file has been modified since last check, update */
        if (stats.st_mtime != d->last_mod)
        {
            get_last_line(d);
            d->last_mod = stats.st_mtime;
            d->state = UPDATED;
        }
    }
}


/* Update the details screen to display info about the selected item */
static void update_details(screen_t *screen, const data_t *selected)
{
    int c, maxx, maxy, bytes;

    /* Clear and get the size of the details window */
    wclear(screen->details);
    getmaxyx(screen->details, maxy, maxx);

    /* Draw last n bytes of file: (figure out how much room we have)   */
    maxy -= 2; /* Ignore border */
    maxx -= 2; /* Ignore border */
    bytes = maxx * maxy;

    /* Set the file-read pointer */
    if (fseek(selected->fp, -bytes, SEEK_END) == -1)
      fseek(selected->fp, 0, SEEK_SET);

    /* Read in file contents */
    wmove(screen->details, 1, 1);
    while ((c = fgetc(selected->fp)) != EOF)
    {
        /* Add whitespace if the cursor is on a border */
        if (getcurx(screen->details) == maxx)
        {
            waddch(screen->details, ' ');
            waddch(screen->details, ' ');
            waddch(screen->details, ' ');
        }
        else if (getcurx(screen->details) == 0)
          waddch(screen->details, ' ');

        waddch(screen->details, c);
    }
    
    /* Display file name and draw border */
    box(screen->details, 0, 0);
    mvwprintw(screen->details, 0, 1, "[%s]", selected->base_name);
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
    int c;
    const data_t *show_details;

    /* Force initial drawing */
    data_update(screen->datas);
    screen_update(screen, NULL);
    show_details = NULL;

    /* Initialize the var (might cause bus error in screen update) */
    show_details = NULL;

    while ((c = getch()) != 'Q' && c != 'q')
    {
        switch (c)
        {
            case KEY_UP:
            case 'k':
                menu_driver(screen->menu, REQ_UP_ITEM);
                break;

            case KEY_DOWN:
            case 'j':
                menu_driver(screen->menu, REQ_DOWN_ITEM);
                break;

            case KEY_ENTER:
            case '\n':
            case 'l':
                show_details = item_userptr(current_item(screen->menu));
                break;

            case 0x1B: /* KEY_ESC */
            case ' ':
            case 'x':
            case 'X':
            case 'h':
                show_details = NULL;

            /* no key striken (timeout), don't modify the screen state */
            case ERR:
            default:
                break;
        }

        data_update(screen->datas);
        screen_update(screen, show_details);
    }
}


int main(int argc, char **argv)
{
    int i, timeout_secs;
    screen_t *screen;
    data_t *datas;
    const char *fname;

    /* Args */
    fname = 0;
    timeout_secs = DEFAULT_TIMEOUT_SECS;
    for (i=1; i<argc; ++i)
    {
        if (strncmp(argv[i], "-d", strlen("-d")) == 0)
        {
            if (i+1 < argc)
              timeout_secs = atoi(argv[++i]);
            else
              usage(argv[0], "Incorrect timeout value specified");
        }
        else if (strncmp(argv[i], "-h", strlen("-h")) == 0)
          usage(argv[0], NULL);
        else if (argv[i][0] != '-')
          fname = argv[i];
        else
          usage(argv[0], "Invalid argument specified");
    }

    /* Sanity check args */
    if (!fname)
      usage(argv[0], "Please provide a configuration file");
    if (timeout_secs < 0)
      usage(argv[0], "Incorrect timeout value specified");

    DBG("Using config:  %s", fname);
    DBG("Using timeout: %d seconds", timeout_secs);

    /* Load data */
    datas = data_init(fname);

    /* Initialize display */
    screen = screen_create(datas, timeout_secs * 1000);

    /* Do the work */
    process(screen);

    /* Cleanup */
    data_destroy(datas);
    screen_destroy(screen);

    return 0;
}
