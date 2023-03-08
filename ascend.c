/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define ASCEND_VERSION "0.1.3"
#define CTRL_KEY(k) ((k)&0x1f)

/*** data ***/

struct editorConfig
{
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

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
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        errhandl("tcsetattr");
}

char editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            errhandl("read");
    }
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

/***  append buffer  ***/
struct abuf{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len){
    char *new = realloc(ab->b, ab->len + len);

    if(new == NULL)
        return;

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab){
    free(ab->b);
}

/*** output ***/

void editorDrawRows( struct abuf *ab)
{
    int lines;
    for (lines = 0; lines < E.screenrows; lines++)
    {
        if(lines == E.screenrows / 3){
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                "Blackmagic Ascend -- version %s", ASCEND_VERSION
            );
            if(welcomelen > E.screencols) 
                welcomelen = E.screencols;
            
            int padding = (E.screencols - welcomelen) / 2;
            if(padding){
                abAppend(ab, "~", 1);
                padding--;
            }
            while(padding--)
                abAppend(ab, " ", 1);

            abAppend(ab, welcome, welcomelen);
        }
        else{

            // commenting out this line to fix the last line bug
            // write(STDOUT_FILENO, "~\r\n", 3);
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);      // erase in-line [http://vt100.net/docs/vt100-ug/chapter3.html#EL]
        if (lines < E.screenrows - 1)
            abAppend(ab, "\r\n", 2);
    }
}

void editorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);  // reset mode [http://vt100.net/docs/vt100-ug/chapter3.html#RM]
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h", 6);  // set mode [http://vt100.net/docs/vt100-ug/chapter3.html#SM]

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/

void editorProcessKeypress()
{
    char c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    }
}

/*** init utils ***/

void editorInit()
{
    E.cx = 0;
    E.cy = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        errhandl("getWindowSize");
}

int main()
{
    enableRawMode();
    editorInit();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}