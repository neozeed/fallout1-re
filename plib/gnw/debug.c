#include "plib/gnw/debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "plib/gnw/intrface.h"
#include "plib/gnw/memory.h"

static int debug_mono(char* string);
static int debug_log(char* string);
static int debug_screen(char* string);
static void debug_putc(int ch);
static void debug_scroll();
static void debug_exit(void);

// 0x539D5C
static FILE* fd = NULL;

// 0x539D60
static int curx = 0;

// 0x539D64
static int cury = 0;

// 0x539D68
static DebugFunc* debug_func = NULL;

// 0x4B2D90
void GNW_debug_init()
{
    atexit(debug_exit);
}

// 0x4B2D9C
void debug_register_mono()
{
    if (debug_func != debug_mono) {
        if (fd != NULL) {
            fclose(fd);
            fd = NULL;
        }

        debug_func = debug_mono;
        debug_clear();
    }
}

// 0x4B2DD8
void debug_register_log(const char* fileName, const char* mode)
{
    if ((mode[0] == 'w' && mode[1] == 'a') && mode[1] == 't') {
        if (fd != NULL) {
            fclose(fd);
        }

        fd = fopen(fileName, mode);
        debug_func = debug_log;
    }
}

// 0x4B2E1C
void debug_register_screen()
{
    if (debug_func != debug_screen) {
        if (fd != NULL) {
            fclose(fd);
            fd = NULL;
        }

        debug_func = debug_screen;
    }
}

// 0x4B2E50
void debug_register_env()
{
	char* copy;
    const char* type = getenv("DEBUGACTIVE");
    if (type == NULL) {
        return;
    }

    copy = (char*)mem_malloc(strlen(type) + 1);
    if (copy == NULL) {
        return;
    }

    strcpy(copy, type);
    strlwr(copy);

    if (strcmp(copy, "mono") == 0) {
        // NOTE: Uninline.
        debug_register_mono();
    } else if (strcmp(copy, "log") == 0) {
        debug_register_log("debug.log", "wt");
    } else if (strcmp(copy, "screen") == 0) {
        // NOTE: Uninline.
        debug_register_screen();
    } else if (strcmp(copy, "gnw") == 0) {
        if (debug_func != win_debug) {
            if (fd != NULL) {
                fclose(fd);
                fd = NULL;
            }

            debug_func = win_debug;
        }
    }

    mem_free(copy);
}

// 0x4B2FD8
void debug_register_func(DebugFunc* proc)
{
    if (debug_func != proc) {
        if (fd != NULL) {
            fclose(fd);
            fd = NULL;
        }

        debug_func = proc;
    }
}

// 0x4B3008
int debug_printf(const char* format, ...)
{
    int rc;
    va_list args;
    va_start(args, format);

    if (debug_func != NULL) {
        char string[260];
        vsprintf(string, format, args);

        rc = debug_func(string);
    } else {
#ifdef _DEBUG
        char string[260];
        vsprintf(string, format, args);
        OutputDebugStringA(string);
#endif
        rc = -1;
    }

    va_end(args);

    return rc;
}

// 0x4B3054
int debug_puts(char* string)
{
    if (debug_func != NULL) {
        return debug_func(string);
    }

    return -1;
}

// 0x4B306C
void debug_clear()
{
    char* buffer;
    int x;
    int y;

    buffer = NULL;

    if (debug_func == debug_mono) {
        buffer = (char*)0xB0000;
    } else if (debug_func == debug_screen) {
        buffer = (char*)0xB8000;
    }

    if (buffer != NULL) {
        for (y = 0; y < 25; y++) {
            for (x = 0; x < 80; x++) {
                *buffer++ = ' ';
                *buffer++ = 7;
            }
        }
        cury = 0;
        curx = 0;
    }
}

// 0x4B30C4
static int debug_mono(char* string)
{
    if (debug_func == debug_mono) {
        while (*string != '\0') {
            char ch = *string++;
            debug_putc(ch);
        }
    }
    return 0;
}

// 0x4B30E8
static int debug_log(char* string)
{
    if (debug_func == debug_log) {
        if (fd == NULL) {
            return -1;
        }

        if (fprintf(fd, string) < 0) {
            return -1;
        }

        if (fflush(fd) == EOF) {
            return -1;
        }
    }

    return 0;
}

// 0x4B3128
static int debug_screen(char* string)
{
    if (debug_func == debug_screen) {
        printf(string);
    }

    return 0;
}

// 0x4B315C
static void debug_putc(int ch)
{
    char* buffer;

    buffer = (char*)0xB0000;

    switch (ch) {
    case 7:
        printf("\x07");
        return;
    case 8:
        if (curx > 0) {
            curx--;
            buffer += 2 * curx + 2 * 80 * cury;
            *buffer++ = ' ';
            *buffer = 7;
        }
        return;
    case 9:
        do {
            debug_putc(' ');
        } while ((curx - 1) % 4 != 0);
        return;
    case 13:
        curx = 0;
        return;
    default:
        buffer += 2 * curx + 2 * 80 * cury;
        *buffer++ = ch;
        *buffer = 7;
        curx++;
        if (curx < 80) {
            return;
        }
        // FALLTHROUGH
    case 10:
        curx = 0;
        cury++;
        if (cury > 24) {
            cury = 24;
            debug_scroll();
        }
        return;
    }
}

// 0x4B326C
static void debug_scroll()
{
    char* buffer;
    int x;
    int y;

    buffer = (char*)0xB0000;

    for (y = 0; y < 24; y++) {
        for (x = 0; x < 80 * 2; x++) {
            buffer[0] = buffer[80 * 2];
            buffer++;
        }
    }

    for (x = 0; x < 80; x++) {
        *buffer++ = ' ';
        *buffer++ = 7;
    }
}

// 0x4B32A8
static void debug_exit(void)
{
    if (fd != NULL) {
        fclose(fd);
    }
}
