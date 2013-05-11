#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <unistd.h>
#include <curses.h>
#include <menu.h>


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


/* File information */
typedef struct _data_t
{
    long val;
    int fd;
    FILE *fp;
    const char *full_path;
    const char *base_name;
    struct _data_t *next;
} data_t;


/* Screen (ncurses state and content) */
typedef struct _screen_t
{
    WINDOW *master;
    WINDOW *content;
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
static void screen_create_menu(screen_t *screen, data_t *data)
{
    int i;
    data_t *d;

    /* Count number of data items */
    for (d=data; d; d=d->next)
      ++i;

    /* Allocate and create menu items (one per data item */
    screen->items = (ITEM **)calloc(i+1, sizeof(ITEM *));
    for (i=0, d=data; d; d=d->next, ++i)
      screen->items[i] = new_item(d->base_name, "POOP");
    set_item_userptr(screen->items[0], NULL);

    screen->menu = new_menu(screen->items);
    set_menu_win(screen->menu, screen->content);
    post_menu(screen->menu);
    wrefresh(screen->content);
}


/* Initialize curses */
static screen_t *screen_create(data_t *datas)
{
    screen_t *screen;
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    screen = calloc(1, sizeof(screen_t));
    screen->master = newwin(LINES, COLS, 0, 0);
    screen->content = newwin(LINES-4, COLS-4, 2, 2);

    box(screen->master, 0, 0);
    scrollok(screen->content, TRUE);
    wrefresh(screen->master);
    screen_create_menu(screen, datas);
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
    int i;
    FILE *fp, *entry_fp;
    data_t *head, *tmp;
    char *c, line[1024] = {0};

    if (!(fp = fopen(fname, "r")))
      ER("Could not open config file '%s'", fname);

    /* For each line in config */
    head = NULL;
    while ((c=fgets(line, sizeof(line)-1, fp)))
    {
        /* Skip whitespace */
        while (*c && isspace(*c))
          ++c;

        if (*c == COMMENT_CHAR)
          break;

        if (strchr(c, COMMENT_CHAR))
          *(strchr(c, COMMENT_CHAR)) = '\0';

        if (!(entry_fp = fopen(c, "r")))
        {
            WR("Could not open file: '%s'", c);
            continue;
        }
       
        /* Add to list */ 
        tmp = head;
        head = calloc(1, sizeof(data_t));
        head->fp = entry_fp;
        head->fd = fileno(entry_fp);
        head->full_path = strdup(c);
        head->base_name = basename(c);
        head->val = ++i;
        head->next = tmp;
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


/* Update data */
static void data_update(data_t *data)
{
}


/* Update display */
static void screen_update(screen)
{
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

    /* Process */
    for ( ;; )
    {
        data_update(datas);
        screen_update(screen);
        sleep(1);
    }

    /* Cleanup */
    data_destroy(datas);
    screen_destroy(screen);

    return 0;
}
