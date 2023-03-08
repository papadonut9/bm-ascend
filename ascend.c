/***  include  ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/***  defines  ***/
#define CTRL_KEY(k) ((k)&0x1f)

/***  data  ***/
struct editorConfig
{
    int screenrows;
    int screencols;
    struct termios orig_termios;
} E;

// struct editorconfig E;

/***  terminal  ***/
void errhandl(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(69);
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
    raw.c_cflag |= ~(CS8);
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

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        return -1;
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/***  output  ***/
void editorDrawRows()
{
    // not this is a poor attempt by me to replicate vim's tildes to represent line numbers
    int lines;
    for (lines = 0; lines < 24; lines++)
        write(STDOUT_FILENO, "~\r\n", 3);
}

void editorRefreshScreen()
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/***  input  ***/
void editorProcessKeypress()
{
    char c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);

        // could've used atexit(), but the error
        // message printed by errhandl() would've been wiped out.

        exit(69);
        break;

    default:
        break;
    }
}

/***  init utils  ***/
void editorInit()
{
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
    // disabled key output printing in debug. will enable that in next itr

    return 0;
}
