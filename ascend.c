/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
#define ASCEND_VERSION "3.4.135 -stable"
#define ASCEND_TAB_STOP 8
#define ASCEND_QUIT_TIMES 2

#define CTRL_KEY(k) ((k)&0x1f)

enum editorKey
{
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum editorHighlight{
    HL_NORMAL = 0,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)

/*** data ***/

struct editorSyntax{
    char *filetype;
    char ** filematch;
    int flags;
};
typedef struct erow
{
    int size;
    int rowsize;
    char *chars;
    char *render;
    unsigned char *highlight;
} erow;

struct editorConfig
{
    int cx, cy;
    int rx;
    int rowoffset;
    int coloffset;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    int dirty;
    char *filename; // status bar only
    char statusmsg[80];
    time_t statusmsg_time;
    struct editorSyntax *syntax;
    struct termios orig_termios;
};

struct editorConfig E;

/***  filetypes  ***/

char *C_Highlight_Extensions[] = {".c", ".h", ".cpp", NULL};

struct editorSyntax HLDB[] = {
    {
        "c",
        C_Highlight_Extensions,
        HL_HIGHLIGHT_NUMBERS
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/***  prototype functions  ***/
void editorSetStatusMsg(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

/*** terminal ***/

void errhandl(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        errhandl("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        errhandl("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 10; // adjust VTIME temporarily to actually see keypresses

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        errhandl("tcsetattr");
}

int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            errhandl("read");
    }

    if (c == '\x1b')
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DEL_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }
        return '\x1b';
    }
    else
        return c;
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';

    // not printing \x1b while printing out the buffer
    if (buf[0] != '\x1b' || buf[1] != '[') // skipping escape sequence in buffer
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    // removing the debug 1 from the if condition temporarily
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/***  syntax highlighting  ***/

int isSeparator(int c){
   
    return 
        isspace(c) ||
        c == '\0' ||
        strchr(",.()+-/*=~%<>[];", c) != NULL;

}

void editorUpdateSyntax(erow *row){
    row->highlight = realloc(row->highlight, row->rowsize);
    memset(row->highlight, HL_NORMAL, row->rowsize);

    int prev_separator = 1;

    int cnt = 0;
    while (cnt < row->rowsize){
        char c = row->render[cnt];
        unsigned char prev_highlight = (cnt > 0)
                                        ? row->highlight[cnt - 1]
                                        : HL_NORMAL;

        if((isdigit(c) && (prev_separator || prev_highlight == HL_NUMBER)) || (c == '.' && prev_highlight == HL_NUMBER)){
            row->highlight[cnt] = HL_NUMBER;
            cnt++;
            prev_separator = 0;
            continue;
        }
        
        prev_separator = isSeparator(c);
        cnt++;
    }
}

int editorSyntaxToColor(int highlight){
    switch (highlight)
    {
    case HL_NUMBER:
        return 31;
    
    case HL_MATCH:
        return 32;

    default:
        return 37;
    }
}

/***  row operations  ***/

int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    int cnt;

    for (cnt = 0; cnt < cx; cnt++)
    {
        if (row->chars[cnt] == '\t')
            rx += (ASCEND_TAB_STOP - 1) - (rx % ASCEND_TAB_STOP);
        rx++;
    }
    return rx;
}

int editorRowRxToCx(erow *row, int rx)
{
    int curr_rx = 0;
    int cx;

    for (cx = 0; cx < row->size; cx++)
    {
        if (row->chars[cx] == '\t')
            curr_rx += (ASCEND_TAB_STOP - 1) - (curr_rx % ASCEND_TAB_STOP);

        curr_rx++;

        if (curr_rx > rx)
            return cx;
    }
    return cx;
}

void editorUpdateRow(erow *row)
{
    int tabs = 0;
    int cnt;

    for (cnt = 0; cnt < row->size; cnt++)
        if (row->chars[cnt] == '\t')
            tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (ASCEND_TAB_STOP - 1) + 1);

    int index = 0;
    for (cnt = 0; cnt < row->size; cnt++)
    {
        if (row->chars[cnt] == '\t')
        {
            row->render[index++] = ' ';
            while (index % ASCEND_TAB_STOP != 0)
                row->render[index++] = ' ';
        }
        else
            row->render[index++] = row->chars[cnt];
    }

    row->render[index] = '\0';
    row->rowsize = index;

    editorUpdateSyntax(row);
}

void editorFreeRow(erow *row)
{
    free(row->render);
    free(row->chars);
    free(row->highlight);
}

void editorDeleteRow(int pos)
{
    if (pos < 0 || pos >= E.numrows)
        return;

    editorFreeRow(&E.row[pos]);
    memmove(&E.row[pos], &E.row[pos + 1], sizeof(erow) * (E.numrows - pos - 1));
    E.numrows--;
    E.dirty++;
}

void editorInsertRow(int pos, char *s, size_t len)
{
    if (pos < 0 || pos > E.numrows)
        return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[pos + 1], &E.row[pos], sizeof(erow) * (E.numrows - pos));

    E.row[pos].size = len;
    E.row[pos].chars = malloc(len + 1);
    memcpy(E.row[pos].chars, s, len);
    E.row[pos].chars[len] = '\0';

    E.row[pos].rowsize = 0;
    E.row[pos].render = NULL;
    E.row[pos].highlight = NULL;
    editorUpdateRow(&E.row[pos]);

    E.numrows++;
    E.dirty++;
}

void editorRowDeleteChar(erow *row, int pos)
{
    if (pos < 0 || pos > row->size)
        return;
    memmove(&row->chars[pos], &row->chars[pos + 1], row->size - pos);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c)
{
    if (at < 0 || at > row->size)
        at = row->size;

    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *str, size_t len)
{
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], str, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

/***  editor operations  ***/

void editorInsertChar(int c)
{
    if (E.cy == E.numrows)
        editorInsertRow(E.numrows, "", 0);

    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorinsertNewLine()
{
    if (E.cx == 0)
        editorInsertRow(E.cy, "", 0);
    else
    {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorDeleteChar()
{
    if (E.cy == E.numrows)
        return;
    if (E.cx == 0 && E.cy == 0)
        return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0)
    {
        editorRowDeleteChar(row, E.cx - 1);
        E.cx--;
    }
    else
    {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDeleteRow(E.cy);
        E.cy--;
    }
}

/***  file I/O  ***/

char *editorRowsToString(int *buffrlen)
{
    int totlen = 0;
    int cnt;
    for (cnt = 0; cnt < E.numrows; cnt++)
        totlen += E.row[cnt].size + 1;
    *buffrlen = totlen;

    char *buffer = malloc(totlen);
    char *ptr = buffer;

    for (cnt = 0; cnt < E.numrows; cnt++)
    {
        memcpy(ptr, E.row[cnt].chars, E.row[cnt].size);
        ptr += E.row[cnt].size;
        *ptr = '\n';
        ptr++;
    }
    return buffer;
}

void editorOpen(char *filename)
{
    // status bar filename
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp)
        errhandl("fopen");

    char *line = NULL;
    ssize_t linelen;
    size_t linecap = 0;
    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {

        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
        editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editorSave()
{
    if (E.filename == NULL)
    {
        E.filename = editorPrompt("Save as: %s\t (esc to cancel)", NULL);
        if (E.filename == NULL)
        {
            editorSetStatusMsg("Save cancelled successfully!!");
            return;
        }
    }

    int len;
    char *buffer = editorRowsToString(&len);

    int fdefine = open(E.filename, O_RDWR | O_CREAT, 0644);

    if (fdefine != -1)
    {

        if (ftruncate(fdefine, len) != -1)
        {
            if (write(fdefine, buffer, len) == len)
            {
                close(fdefine);
                free(buffer);
                E.dirty = 0;
                editorSetStatusMsg("%d bytes written to disk", len);
                return;
            }
        }
        close(fdefine);
    }

    free(buffer);
    editorSetStatusMsg("Can't save!! i/o error: %s", strerror(errno));
}

/***  search  ***/
void editorFindCallback(char *query, int key)
{
    static int last_match = -1;
    static int direction = 1;
    static int saved_highlight_line;
    static char *saved_highlight = NULL;

    if(saved_highlight){
        memcpy(E.row[saved_highlight_line].highlight, saved_highlight, E.row[saved_highlight_line].rowsize);
        free(saved_highlight);
        saved_highlight = NULL;
    }

    if (key == '\r' || key == '\x1b')
    {
        last_match = -1;
        direction = -1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN)
        direction = 1;

    else if (key == ARROW_LEFT || key == ARROW_UP)
        direction = -1;

    else
    {
        last_match = -1;
        direction = -1;
    }

    if (last_match == -1)
        direction = 1;

    int current = last_match;
    int cnt;

    for (cnt = 0; cnt < E.numrows; cnt++)
    {
        current += direction;

        if (current == -1)
            current = E.numrows - 1;
        else if (current == E.numrows)
            current = 0;

        erow *row = &E.row[current];
        char *match = strstr(row->render, query);
        if (match)
        {
            last_match = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);
            E.rowoffset = E.numrows;

            saved_highlight_line = current;
            saved_highlight = malloc(row->rowsize);
            memcpy(saved_highlight, row->highlight, row->rowsize);

            memset(&row->highlight[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editorFind()
{
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloffset = E.coloffset;
    int saved_rowoffset = E.rowoffset;

    char *query = editorPrompt("Search: %s\t(Use esc/arrows/return)",
                               editorFindCallback);
    if (query)
        free(query);
    else
    {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloffset = saved_coloffset;
        E.rowoffset = saved_rowoffset;
    }
}

/***  append buffer  ***/
struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT \
    {             \
        NULL, 0   \
    }

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/*** output ***/

void editorScroll()
{
    // tab rendering
    E.rx = 0;
    if (E.cy < E.numrows)
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

    // Vertical Scrolling
    if (E.cy < E.rowoffset)
        E.rowoffset = E.cy;

    if (E.cy >= E.rowoffset + E.screenrows)
        E.rowoffset = E.cy - E.screenrows + 1;

    // Horizontal Scrolling
    if (E.rx < E.coloffset)
        E.coloffset = E.rx;

    if (E.rx >= E.coloffset + E.screencols)
        E.coloffset = E.rx - E.screencols + 1;
}

void editorDrawRows(struct abuf *ab)
{
    int lines;
    for (lines = 0; lines < E.screenrows; lines++)
    {
        int filerow = lines + E.rowoffset;
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && lines == E.screenrows / 3)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                          "Blackmagic Ascend -v%s", ASCEND_VERSION);
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;

                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);

                abAppend(ab, welcome, welcomelen);
            }
            else
            {

                // commenting out this line to fix the last line bug
                // write(STDOUT_FILENO, "~\r\n", 3);
                abAppend(ab, "~", 1);
            }
        }
        else
        {
            int len = E.row[filerow].rowsize - E.coloffset;

            if (len < 0)
                len = 0;

            if (len > E.screencols)
                len = E.screencols;

            char *c  = &E.row[filerow].render[E.coloffset];
            unsigned char *highlight = &E.row[filerow].highlight[E.coloffset];
            int curr_color = -1;
            int cnt;
            
            for(cnt = 0; cnt < len; cnt++){
                if(highlight[cnt] == HL_NORMAL){
                    if(curr_color != -1){
                        abAppend(ab, "\x1b[39m", 5);
                        curr_color = -1;
                    }
                    abAppend(ab, &c[cnt], 1);
                }
                else{
                    int color = editorSyntaxToColor(highlight[cnt]);
                    if(color != curr_color){
                        curr_color = color;
                        char buffer[16];
                        int clength = snprintf(buffer, sizeof(buffer), "\x1b[%dm", color);
                        abAppend(ab, buffer, clength);
                    }
                    abAppend(ab, &c[cnt], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
        }

        abAppend(ab, "\x1b[K", 3); // erase in-line [http://vt100.net/docs/vt100-ug/chapter3.html#EL]
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[7m", 4); // selective graphic rendition [http://vt100.net/docs/vt100-ug/chapter3.html#SGR]

    char rstatus[80];
    char status[80];
    int len = snprintf(status,
                       sizeof(status),
                       "%.20s - %d lines %s",
                       E.filename
                           ? E.filename
                           : "[NO FILE]",
                       E.numrows,
                       E.dirty
                           ? "(modified)"
                           : "");

    int rlen = snprintf(
                rstatus, 
                
                sizeof(rstatus), 
                
                "%s | %d/%d", 
                
                E.syntax
                ? E.syntax->filetype
                : "no filetype",

                E.cy + 1, 
                
                E.numrows);

    if (len > E.screencols)
        len = E.screencols;

    abAppend(ab, status, len);

    while (len < E.screencols)
    {
        if (E.screencols - len == rlen)
        {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }

    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorRenderMsgBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols)
        msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen()
{
    editorScroll();
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // reset mode [http://vt100.net/docs/vt100-ug/chapter3.html#RM]
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorRenderMsgBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoffset) + 1, (E.rx - E.coloffset) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // set mode [http://vt100.net/docs/vt100-ug/chapter3.html#SM]

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMsg(const char *formatstr, ...)
{
    va_list ap;
    va_start(ap, formatstr);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), formatstr, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int))
{
    size_t buffrsize = 128;
    char *buffer = malloc(buffrsize);

    size_t buflen = 0;
    buffer[0] = '\0';

    while (1)
    {
        editorSetStatusMsg(prompt, buffer);
        editorRefreshScreen();

        int c = editorReadKey();

        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
        {
            if (buflen != 0)
                buffer[--buflen] = '\0';
        }
        else if (c == '\x1b')
        {
            editorSetStatusMsg("");

            if (callback)
                callback(buffer, c);

            free(buffer);
            return NULL;
        }
        else if (c == '\r')
        {
            if (buflen != 0)
            {
                editorSetStatusMsg("");
                if (callback)
                    callback(buffer, c);

                return buffer;
            }
        }
        else if (!iscntrl(c) && c < 128)
        {
            if (buflen == buffrsize - 1)
            {
                buffrsize *= 2;
                buffer = realloc(buffer, buffrsize);
            }
            buffer[buflen++] = c;
            buffer[buflen] = '\0';
        }
        if (callback)
            callback(buffer, c);
    }
}

void editorMoveCursor(int key)
{
    erow *row = (E.cy >= E.numrows)
                    ? NULL
                    : &E.row[E.cy];

    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
            E.cx--;
        else if (E.cy > 0)
        {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }

        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size)
            E.cx++;
        else if (row && E.cx == row->size)
        {
            E.cy++;
            E.cx = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0)
            E.cy--;
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows)
            E.cy++;
        break;
    }
}

void editorProcessKeypress()
{
    static int quit_times = ASCEND_QUIT_TIMES;
    int c = editorReadKey();

    switch (c)
    {
    case '\r':
        editorinsertNewLine();
        break;

    case CTRL_KEY('q'):
        if (E.dirty && quit_times > 0)
        {
            editorSetStatusMsg(
                "WARNING! File has unsaved changes."
                "Press ctrl-q %d more time(s) to quit.",
                quit_times);
            quit_times--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case CTRL_KEY('s'):
        editorSave();
        break;

    case HOME_KEY:
        E.cx = 0;
        break;

    case END_KEY:
        if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
        break;

    case CTRL_KEY('f'):
        editorFind();
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if (c == DEL_KEY)
            editorMoveCursor(ARROW_RIGHT);
        editorDeleteChar();
        break;

    case PAGE_UP:
    case PAGE_DOWN:
    {
        if (c == PAGE_UP)
            E.cy = E.rowoffset;
        else if (c == PAGE_DOWN)
        {
            E.cy = E.rowoffset + E.screenrows - 1;
            if (E.cy > E.numrows)
                E.cy = E.numrows;
        }

        int times = E.screenrows;
        while (times--)
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
    break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        editorInsertChar(c);
        break;
    }
    quit_times = ASCEND_QUIT_TIMES;
}

/*** init utils ***/

void editorInit()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoffset = 0;
    E.coloffset = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.syntax = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        errhandl("getWindowSize");
    E.screenrows -= 2;
}

int main(int argc, char *argv[])
{
    enableRawMode();
    editorInit();

    if (argc >= 2)
        editorOpen(argv[1]);

    editorSetStatusMsg("HELP: ctrl-q: quit  |   ctrl-s: save    |   ctrl-f: search");
                                                                                                                                                                                            
    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}