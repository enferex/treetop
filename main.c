#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <unistd.h>
#include <curses.h>
#include <menu.h>
#include <sys/stat.h>


/* Output routines */
#define _PR(_tag, _outfd, ...)                       \
    do {                                             \
        fprintf(_outfd, "[logtop]"_tag __VA_ARGS__); \
         fputc('\n', _outfd);                        \
    } while(0)
#define PR(...)  _PR(" ", stdout, __VA_ARGS__)
#define DBG(...) _PR("[debug] ", stdout, __VA_ARGS__)
#define WR(...)  _PR("[warning] ", stderr, __VA_ARGS__)
#define ER(...)  _PR("[error] ", stderr, __VA_ARGS__)


/* Comment character for config file (anything after this char is ignored) */
#define COMMENT_CHAR '#'


/* If the file has the 'UPDATED' state */
#define UPDATED_CHAR '*'


/* Delay milliseconds */
#define DELAY_MS 1000


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

    /* Count number of data items */
    for (d=screen->datas; d; d=d->next)
      ++i;

    /* Allocate and create menu items (one per data item */
    screen->items = (ITEM **)calloc(i+1, sizeof(ITEM *));
    for (i=0, d=screen->datas; d; d=d->next, ++i)
    {
        screen->items[i] = new_item(d->base_name, "Updating...");
        d->item = screen->items[i];
    }

    screen->menu = new_menu(screen->items);
    set_menu_mark(screen->menu, "--> ");
    set_menu_win(screen->menu, screen->content);
    post_menu(screen->menu);
}


/* Initialize curses */
static screen_t *screen_create(data_t *datas)
{
    screen_t *screen;
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(DELAY_MS);

    screen = calloc(1, sizeof(screen_t));
    screen->master = newwin(LINES, COLS, 0, 0);
    screen->content = newwin(LINES-4, COLS-4, 2, 2);
    screen->datas = datas;

    box(screen->master, 0, 0);
    mvwprintw(screen->master, 0, COLS/2 - 6, "}-= PTOP =-{");
    scrollok(screen->content, TRUE);
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
            CONTINUE;;
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

    /* Get file size */

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
            d->last_mod = stat.st_mtime;
            get_last_line(d);
            if (d->item)
            {
                d->item->description.str = d->line;
                d->item->description.length = strlen(d->line);
                 
            }
        }
    }
}


/* Update display */
static void screen_update(screen_t *screen)
{
    /* Refresh menu */
    unpost_menu(screen->menu);
    post_menu(screen->menu);
    wnoutrefresh(screen->master);
    wnoutrefresh(screen->content);
    doupdate();
}


static void process(screen_t *screen)
{
    int c;

    while ((c = getch()) != 'Q' && c != 'q')
    {
        switch (c)
        {
            case KEY_UP:
                menu_driver(screen->menu, REQ_UP_ITEM);
                break;
            case KEY_DOWN:
                menu_driver(screen->menu, REQ_DOWN_ITEM);
                break;
            case KEY_ENTER:
                break;
            default:
                break;
        }
        data_update(screen->datas);
        screen_update(screen);
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
