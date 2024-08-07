#include "plib/gnw/input.h"

#include <stdio.h>
#include <limits.h> //INT_MAX

#include "plib/color/color.h"
#include "plib/gnw/button.h"
#include "plib/gnw/dxinput.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/intrface.h"
#include "plib/gnw/memory.h"
#include "plib/gnw/mmx.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"
#include "plib/gnw/vcr.h"
#include "plib/gnw/winmain.h"

typedef struct GNW95RepeatStruct {
    // Time when appropriate key was pressed down or -1 if it's up.
    unsigned int time;
    unsigned short count;
} GNW95RepeatStruct;

typedef struct inputdata {
    // This is either logical key or input event id, which can be either
    // character code pressed or some other numbers used throughout the
    // game interface.
    int input;
    int mx;
    int my;
} inputdata;

typedef struct funcdata {
    unsigned int flags;
    BackgroundProcess* f;
    struct funcdata* next;
} funcdata;

typedef funcdata* FuncPtr;

static int get_input_buffer();
static void pause_game();
static int default_pause_window();
static void buf_blit(unsigned char* src, unsigned int src_pitch, unsigned int a3, unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int dest_x, unsigned int dest_y);
static void GNW95_build_key_map();
static int GNW95_hook_keyboard(int hook);
static void GNW95_process_key(dxinput_key_data* data);

// 0x539D6C
static IdleFunc* idle_func = NULL;

// 0x539D70
static FocusFunc* focus_func = NULL;

// 0x539D74
static unsigned int GNW95_repeat_rate = 80;

// 0x539D78
static unsigned int GNW95_repeat_delay = 500;

// A map of DIK_* constants normalized for QWERTY keyboard.
//
// 0x6713F0
unsigned char GNW95_key_map[256];

// Ring buffer of input events.
//
// Looks like this buffer does not support overwriting of values. Once the
// buffer is full it will not overwrite values until they are dequeued.
//
// 0x6714F0
static inputdata input_buffer[40];

// 0x6716D0
GNW95RepeatStruct GNW95_key_time_stamps[256];

// 0x671ED0
static int input_mx;

// 0x671ED4
static int input_my;

// 0x671ED8
static FuncPtr bk_list;

// 0x671EDC
static HHOOK GNW95_keyboardHandle;

// 0x671EE0
static bool game_paused;

// 0x671EE4
static int screendump_key;

// 0x671EE8
static int using_msec_timer;

// 0x671EEC
static int pause_key;

// 0x671EF0
static ScreenDumpFunc* screendump_func;

// 0x671EF4
static int input_get;

// 0x671EF8
static unsigned char* screendump_buf;

// 0x671EFC
static PauseWinFunc* pause_win_func;

// 0x671F00
static int input_put;

// 0x671F04
static bool bk_disabled;

// 0x671F08
static unsigned int bk_process_time;

// 0x4B32C0
int GNW_input_init(int use_msec_timer)
{
    if (!dxinput_init()) {
        return -1;
    }

    if (GNW_kb_set() == -1) {
        return -1;
    }

    if (GNW_mouse_init() == -1) {
        return -1;
    }

    if (GNW95_input_init() == -1) {
        return -1;
    }

    GNW95_hook_input(1);
    GNW95_build_key_map();
    GNW95_clear_time_stamps();

    using_msec_timer = use_msec_timer;
    input_put = 0;
    input_get = -1;
    input_mx = -1;
    input_my = -1;
    bk_disabled = 0;
    game_paused = false;
    pause_key = KEY_ALT_P;
    pause_win_func = default_pause_window;
    screendump_func = default_screendump;
    bk_list = NULL;
    screendump_key = KEY_ALT_C;

    return 0;
}

// 0x4B3390
void GNW_input_exit()
{
	FuncPtr curr;

    // NOTE: Uninline.
    GNW95_input_exit();
    GNW_mouse_exit();
    GNW_kb_restore();
    dxinput_exit();

    curr = bk_list;
    while (curr != NULL) {
        FuncPtr next = curr->next;
        mem_free(curr);
        curr = next;
    }
}

// 0x4B33C8
int get_input()
{
    int v3;

    GNW95_process_message();

    if (!GNW95_isActive) {
        GNW95_lost_focus();
    }

    process_bk();

    v3 = get_input_buffer();
    if (v3 == -1 && mouse_get_buttons() & 0x33) {
        mouse_get_position(&input_mx, &input_my);
        return -2;
    } else {
        return GNW_check_menu_bars(v3);
    }

    return -1;
}

// 0x4B3418
void get_input_position(int* x, int* y)
{
    *x = input_mx;
    *y = input_my;
}

// 0x4B342C
void process_bk()
{
    int v1;

    GNW_do_bk_process();

    if (vcr_update() != 3) {
        mouse_info();
    }

    v1 = win_check_all_buttons();
    if (v1 != -1) {
        GNW_add_input_buffer(v1);
        return;
    }

    v1 = kb_getch();
    if (v1 != -1) {
        GNW_add_input_buffer(v1);
        return;
    }
}

// 0x4B3454
void GNW_add_input_buffer(int a1)
{
	inputdata* inputEvent;

    if (a1 == -1) {
        return;
    }

    if (a1 == pause_key) {
        pause_game();
        return;
    }

    if (a1 == screendump_key) {
        dump_screen();
        return;
    }

    if (input_put == input_get) {
        return;
    }

    inputEvent = &(input_buffer[input_put]);
    inputEvent->input = a1;

    mouse_get_position(&(inputEvent->mx), &(inputEvent->my));

    input_put++;

    if (input_put == 40) {
        input_put = 0;
        return;
    }

    if (input_get == -1) {
        input_get = 0;
    }
}

// 0x4B34E4
static int get_input_buffer()
{
    inputdata* inputEvent;
    int eventCode;

    if (input_get == -1) {
        return -1;
    }

    inputEvent = &(input_buffer[input_get]);
    eventCode = inputEvent->input;
    input_mx = inputEvent->mx;
    input_my = inputEvent->my;

    input_get++;

    if (input_get == 40) {
        input_get = 0;
    }

    if (input_get == input_put) {
        input_get = -1;
        input_put = 0;
    }

    return eventCode;
}

// 0x4B354C
void flush_input_buffer()
{
    input_get = -1;
    input_put = 0;
}

// 0x4B3564
void GNW_do_bk_process()
{
    FuncPtr curr;
    FuncPtr* currPtr;

    if (game_paused) {
        return;
    }

    if (bk_disabled) {
        return;
    }

    bk_process_time = get_time();

    curr = bk_list;
    currPtr = &(bk_list);

    while (curr != NULL) {
        FuncPtr next = curr->next;
        if (curr->flags & 1) {
            *currPtr = next;

            mem_free(curr);
        } else {
            curr->f();
            currPtr = &(curr->next);
        }
        curr = next;
    }
}

// 0x4B35BC
void add_bk_process(BackgroundProcess* f)
{
    FuncPtr fp;

    fp = bk_list;
    while (fp != NULL) {
        if (fp->f == f) {
            if ((fp->flags & 0x01) != 0) {
                fp->flags &= ~0x01;
                return;
            }
        }
        fp = fp->next;
    }

    fp = (FuncPtr)mem_malloc(sizeof(*fp));
    fp->flags = 0;
    fp->f = f;
    fp->next = bk_list;
    bk_list = fp;
}

// 0x4B360C
void remove_bk_process(BackgroundProcess* f)
{
    FuncPtr fp;

    fp = bk_list;
    while (fp != NULL) {
        if (fp->f == f) {
            fp->flags |= 0x01;
            return;
        }
        fp = fp->next;
    }
}

// 0x4B362C
void enable_bk()
{
    bk_disabled = false;
}

// 0x4B3638
void disable_bk()
{
    bk_disabled = true;
}

// 0x4B3644
static void pause_game()
{
	int win;
    if (!game_paused) {
        game_paused = true;

        win = pause_win_func();

        while (get_input() != KEY_ESCAPE) {
        }

        game_paused = false;
        win_delete(win);
    }
}

// 0x4B3680
static int default_pause_window()
{
    int windowWidth = text_width("Paused") + 32;
    int windowHeight = 3 * text_height() + 16;
	unsigned char* windowBuffer;

    int win = win_add((rectGetWidth(&scr_size) - windowWidth) / 2,
        (rectGetHeight(&scr_size) - windowHeight) / 2,
        windowWidth,
        windowHeight,
        256,
        WINDOW_FLAG_0x10 | WINDOW_FLAG_0x04);
    if (win == -1) {
        return -1;
    }

    win_border(win);

    windowBuffer = win_get_buf(win);
    text_to_buf(windowBuffer + 8 * windowWidth + 16,
        "Paused",
        windowWidth,
        windowWidth,
        colorTable[31744]);

    win_register_text_button(win,
        (windowWidth - text_width("Done") - 16) / 2,
        windowHeight - 8 - text_height() - 6,
        -1,
        -1,
        -1,
        KEY_ESCAPE,
        "Done",
        0);

    win_draw(win);

    return win;
}

// 0x4B377C
void register_pause(int new_pause_key, PauseWinFunc* new_pause_win_func)
{
    pause_key = new_pause_key;

    if (new_pause_win_func == NULL) {
        new_pause_win_func = default_pause_window;
    }

    pause_win_func = new_pause_win_func;
}

// 0x4B3794
void dump_screen()
{
    ScreenBlitFunc* old_scr_blit;
    ScreenBlitFunc* old_mouse_blit;
    ScreenTransBlitFunc* old_mouse_blit_trans;
    int width;
    int length;
    unsigned char* pal;

    width = scr_size.lrx - scr_size.ulx + 1;
    length = scr_size.lry - scr_size.uly + 1;
    screendump_buf = (unsigned char*)mem_malloc(width * length);
    if (screendump_buf == NULL) {
        return;
    }

    old_scr_blit = scr_blit;
    scr_blit = buf_blit;

    old_mouse_blit = mouse_blit;
    mouse_blit = buf_blit;

    old_mouse_blit_trans = mouse_blit_trans;
    mouse_blit_trans = NULL;

    win_refresh_all(&scr_size);

    mouse_blit_trans = old_mouse_blit_trans;
    mouse_blit = old_mouse_blit;
    scr_blit = old_scr_blit;

    pal = getSystemPalette();
    screendump_func(width, length, screendump_buf, pal);
    mem_free(screendump_buf);
}

// 0x4B3838
static void buf_blit(unsigned char* src, unsigned int srcPitch, unsigned int a3, unsigned int srcX, unsigned int srcY, unsigned int width, unsigned int height, unsigned int destX, unsigned int destY)
{
    int destWidth = scr_size.lrx - scr_size.ulx + 1;
    buf_to_buf(src + srcPitch * srcY + srcX, width, height, srcPitch, screendump_buf + destWidth * destY + destX, destWidth);
}

// 0x4B3890
int default_screendump(int width, int height, unsigned char* data, unsigned char* palette)
{
    char fileName[16];
    FILE* stream;
    int index;
    unsigned int intValue;
    unsigned short shortValue;
	int x,y;

    for (index = 0; index < 100000; index++) {
        sprintf(fileName, "scr%.5d.bmp", index);

        stream = fopen(fileName, "rb");
        if (stream == NULL) {
            break;
        }

        fclose(stream);
    }

    if (index == 100000) {
        return -1;
    }

    stream = fopen(fileName, "wb");
    if (stream == NULL) {
        return -1;
    }

    // bfType
    shortValue = 0x4D42;
    fwrite(&shortValue, sizeof(shortValue), 1, stream);

    // bfSize
    // 14 - sizeof(BITMAPFILEHEADER)
    // 40 - sizeof(BITMAPINFOHEADER)
    // 1024 - sizeof(RGBQUAD) * 256
    intValue = width * height + 14 + 40 + 1024;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    // bfReserved1
    shortValue = 0;
    fwrite(&shortValue, sizeof(shortValue), 1, stream);

    // bfReserved2
    shortValue = 0;
    fwrite(&shortValue, sizeof(shortValue), 1, stream);

    // bfOffBits
    intValue = 14 + 40 + 1024;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    // biSize
    intValue = 40;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    // biWidth
    intValue = width;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    // biHeight
    intValue = height;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    // biPlanes
    shortValue = 1;
    fwrite(&shortValue, sizeof(shortValue), 1, stream);

    // biBitCount
    shortValue = 8;
    fwrite(&shortValue, sizeof(shortValue), 1, stream);

    // biCompression
    intValue = 0;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    // biSizeImage
    intValue = 0;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    // biXPelsPerMeter
    intValue = 0;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    // biYPelsPerMeter
    intValue = 0;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    // biClrUsed
    intValue = 0;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    // biClrImportant
    intValue = 0;
    fwrite(&intValue, sizeof(intValue), 1, stream);

    for (index = 0; index < 256; index++) {
        unsigned char rgbReserved = 0;
        unsigned char rgbRed = palette[index * 3] << 2;
        unsigned char rgbGreen = palette[index * 3 + 1] << 2;
        unsigned char rgbBlue = palette[index * 3 + 2] << 2;

        fwrite(&rgbBlue, sizeof(rgbBlue), 1, stream);
        fwrite(&rgbGreen, sizeof(rgbGreen), 1, stream);
        fwrite(&rgbRed, sizeof(rgbRed), 1, stream);
        fwrite(&rgbReserved, sizeof(rgbReserved), 1, stream);
    }

    for (y = height - 1; y >= 0; y--) {
        unsigned char* dataPtr = data + y * width;
        fwrite(dataPtr, 1, width, stream);
    }

    fflush(stream);
    fclose(stream);

    return 0;
}

// 0x4B3BA0
void register_screendump(int new_screendump_key, ScreenDumpFunc* new_screendump_func)
{
    screendump_key = new_screendump_key;

    if (new_screendump_func == NULL) {
        new_screendump_func = default_screendump;
    }

    screendump_func = new_screendump_func;
}

// 0x4B3BB8
unsigned int get_time()
{
#pragma warning(suppress : 28159)
    return GetTickCount();
}

// 0x4B3BC4
void pause_for_tocks(unsigned int delay)
{
    // NOTE: Uninline.
    unsigned int start = get_time();
    unsigned int end = get_time();

    // NOTE: Uninline.
    unsigned int diff = elapsed_tocks(end, start);
    while (diff < delay) {
        process_bk();

        end = get_time();

        // NOTE: Uninline.
        diff = elapsed_tocks(end, start);
    }
}

// 0x4B3C00
void block_for_tocks(unsigned int ms)
{
#pragma warning(suppress : 28159)
    unsigned int start = GetTickCount();
    unsigned int diff;
    do {
        // NOTE: Uninline
        diff = elapsed_time(start);
    } while (diff < ms);
}

// 0x4B3C28
unsigned int elapsed_time(unsigned int start)
{
#pragma warning(suppress : 28159)
    unsigned int end = GetTickCount();

    // NOTE: Uninline.
    return elapsed_tocks(end, start);
}

// 0x4B3C48
unsigned int elapsed_tocks(unsigned int end, unsigned int start)
{
    if (start > end) {
        return INT_MAX;
    } else {
        return end - start;
    }
}

// 0x4B3C58
unsigned int get_bk_time()
{
    return bk_process_time;
}

// 0x4B3C60
void set_repeat_rate(unsigned int rate)
{
    GNW95_repeat_rate = rate;
}

// 0x4B3C68
unsigned int get_repeat_rate()
{
    return GNW95_repeat_rate;
}

// 0x4B3C70
void set_repeat_delay(unsigned int delay)
{
    GNW95_repeat_delay = delay;
}

// 0x4B3C78
unsigned int get_repeat_delay()
{
    return GNW95_repeat_delay;
}

// 0x4B3C80
void set_focus_func(FocusFunc* new_focus_func)
{
    focus_func = new_focus_func;
}

// 0x4B3C88
FocusFunc* get_focus_func()
{
    return focus_func;
}

// 0x4B3C90
void set_idle_func(IdleFunc* new_idle_func)
{
    idle_func = new_idle_func;
}

// 0x4B3C98
IdleFunc* get_idle_func()
{
    return idle_func;
}

// 0x4B3CD8
static void GNW95_build_key_map()
{
    unsigned char* keys = GNW95_key_map;
    int k;

    keys[DIK_ESCAPE] = DIK_ESCAPE;
    keys[DIK_1] = DIK_1;
    keys[DIK_2] = DIK_2;
    keys[DIK_3] = DIK_3;
    keys[DIK_4] = DIK_4;
    keys[DIK_5] = DIK_5;
    keys[DIK_6] = DIK_6;
    keys[DIK_7] = DIK_7;
    keys[DIK_8] = DIK_8;
    keys[DIK_9] = DIK_9;
    keys[DIK_0] = DIK_0;

    switch (kb_layout) {
    case 0:
        k = DIK_MINUS;
        break;
    case 1:
        k = DIK_6;
        break;
    default:
        k = DIK_SLASH;
        break;
    }
    keys[DIK_MINUS] = k;

    switch (kb_layout) {
    case 1:
        k = DIK_0;
        break;
    default:
        k = DIK_EQUALS;
        break;
    }
    keys[DIK_EQUALS] = k;

    keys[DIK_BACK] = DIK_BACK;
    keys[DIK_TAB] = DIK_TAB;

    switch (kb_layout) {
    case 1:
        k = DIK_A;
        break;
    default:
        k = DIK_Q;
        break;
    }
    keys[DIK_Q] = k;

    switch (kb_layout) {
    case 1:
        k = DIK_Z;
        break;
    default:
        k = DIK_W;
        break;
    }
    keys[DIK_W] = k;

    keys[DIK_E] = DIK_E;
    keys[DIK_R] = DIK_R;
    keys[DIK_T] = DIK_T;

    switch (kb_layout) {
    case 0:
    case 1:
    case 3:
    case 4:
        k = DIK_Y;
        break;
    default:
        k = DIK_Z;
        break;
    }
    keys[DIK_Y] = k;

    keys[DIK_U] = DIK_U;
    keys[DIK_I] = DIK_I;
    keys[DIK_O] = DIK_O;
    keys[DIK_P] = DIK_P;

    switch (kb_layout) {
    case 0:
    case 3:
    case 4:
        k = DIK_LBRACKET;
        break;
    case 1:
        k = DIK_5;
        break;
    default:
        k = DIK_8;
        break;
    }
    keys[DIK_LBRACKET] = k;

    switch (kb_layout) {
    case 0:
    case 3:
    case 4:
        k = DIK_RBRACKET;
        break;
    case 1:
        k = DIK_MINUS;
        break;
    default:
        k = DIK_9;
        break;
    }
    keys[DIK_RBRACKET] = k;

    keys[DIK_RETURN] = DIK_RETURN;
    keys[DIK_LCONTROL] = DIK_LCONTROL;

    switch (kb_layout) {
    case 1:
        k = DIK_Q;
        break;
    default:
        k = DIK_A;
        break;
    }
    keys[DIK_A] = k;

    keys[DIK_S] = DIK_S;
    keys[DIK_D] = DIK_D;
    keys[DIK_F] = DIK_F;
    keys[DIK_G] = DIK_G;
    keys[DIK_H] = DIK_H;
    keys[DIK_J] = DIK_J;
    keys[DIK_K] = DIK_K;
    keys[DIK_L] = DIK_L;

    switch (kb_layout) {
    case 0:
        k = DIK_SEMICOLON;
        break;
    default:
        k = DIK_COMMA;
        break;
    }
    keys[DIK_SEMICOLON] = k;

    switch (kb_layout) {
    case 0:
        k = DIK_APOSTROPHE;
        break;
    case 1:
        k = DIK_4;
        break;
    default:
        k = DIK_MINUS;
        break;
    }
    keys[DIK_APOSTROPHE] = k;

    switch (kb_layout) {
    case 0:
        k = DIK_GRAVE;
        break;
    case 1:
        k = DIK_2;
        break;
    case 3:
    case 4:
        k = 0;
        break;
    default:
        k = DIK_RBRACKET;
        break;
    }
    keys[DIK_GRAVE] = k;

    keys[DIK_LSHIFT] = DIK_LSHIFT;

    switch (kb_layout) {
    case 0:
        k = DIK_BACKSLASH;
        break;
    case 1:
        k = DIK_8;
        break;
    case 3:
    case 4:
        k = DIK_GRAVE;
        break;
    default:
        k = DIK_Y;
        break;
    }
    keys[DIK_BACKSLASH] = k;

    switch (kb_layout) {
    case 0:
    case 3:
    case 4:
        k = DIK_Z;
        break;
    case 1:
        k = DIK_W;
        break;
    default:
        k = DIK_Y;
        break;
    }
    keys[DIK_Z] = k;

    keys[DIK_X] = DIK_X;
    keys[DIK_C] = DIK_C;
    keys[DIK_V] = DIK_V;
    keys[DIK_B] = DIK_B;
    keys[DIK_N] = DIK_N;

    switch (kb_layout) {
    case 1:
        k = DIK_SEMICOLON;
        break;
    default:
        k = DIK_M;
        break;
    }
    keys[DIK_M] = k;

    switch (kb_layout) {
    case 1:
        k = DIK_M;
        break;
    default:
        k = DIK_COMMA;
        break;
    }
    keys[DIK_COMMA] = k;

    switch (kb_layout) {
    case 1:
        k = DIK_COMMA;
        break;
    default:
        k = DIK_PERIOD;
        break;
    }
    keys[DIK_PERIOD] = k;

    switch (kb_layout) {
    case 0:
        k = DIK_SLASH;
        break;
    case 1:
        k = DIK_PERIOD;
        break;
    default:
        k = DIK_7;
        break;
    }
    keys[DIK_SLASH] = k;

    keys[DIK_RSHIFT] = DIK_RSHIFT;
    keys[DIK_MULTIPLY] = DIK_MULTIPLY;
    keys[DIK_SPACE] = DIK_SPACE;
    keys[DIK_LMENU] = DIK_LMENU;
    keys[DIK_CAPITAL] = DIK_CAPITAL;
    keys[DIK_F1] = DIK_F1;
    keys[DIK_F2] = DIK_F2;
    keys[DIK_F3] = DIK_F3;
    keys[DIK_F4] = DIK_F4;
    keys[DIK_F5] = DIK_F5;
    keys[DIK_F6] = DIK_F6;
    keys[DIK_F7] = DIK_F7;
    keys[DIK_F8] = DIK_F8;
    keys[DIK_F9] = DIK_F9;
    keys[DIK_F10] = DIK_F10;
    keys[DIK_NUMLOCK] = DIK_NUMLOCK;
    keys[DIK_SCROLL] = DIK_SCROLL;
    keys[DIK_NUMPAD7] = DIK_NUMPAD7;
    keys[DIK_NUMPAD9] = DIK_NUMPAD9;
    keys[DIK_NUMPAD8] = DIK_NUMPAD8;
    keys[DIK_SUBTRACT] = DIK_SUBTRACT;
    keys[DIK_NUMPAD4] = DIK_NUMPAD4;
    keys[DIK_NUMPAD5] = DIK_NUMPAD5;
    keys[DIK_NUMPAD6] = DIK_NUMPAD6;
    keys[DIK_ADD] = DIK_ADD;
    keys[DIK_NUMPAD1] = DIK_NUMPAD1;
    keys[DIK_NUMPAD2] = DIK_NUMPAD2;
    keys[DIK_NUMPAD3] = DIK_NUMPAD3;
    keys[DIK_NUMPAD0] = DIK_NUMPAD0;
    keys[DIK_DECIMAL] = DIK_DECIMAL;
    keys[DIK_F11] = DIK_F11;
    keys[DIK_F12] = DIK_F12;
    keys[DIK_F13] = -1;
    keys[DIK_F14] = -1;
    keys[DIK_F15] = -1;
    keys[DIK_KANA] = -1;
    keys[DIK_CONVERT] = -1;
    keys[DIK_NOCONVERT] = -1;
    keys[DIK_YEN] = -1;
    keys[DIK_NUMPADEQUALS] = -1;
    keys[DIK_PREVTRACK] = -1;
    keys[DIK_AT] = -1;
    keys[DIK_COLON] = -1;
    keys[DIK_UNDERLINE] = -1;
    keys[DIK_KANJI] = -1;
    keys[DIK_STOP] = -1;
    keys[DIK_AX] = -1;
    keys[DIK_UNLABELED] = -1;
    keys[DIK_NUMPADENTER] = DIK_NUMPADENTER;
    keys[DIK_RCONTROL] = DIK_RCONTROL;
    keys[DIK_NUMPADCOMMA] = -1;
    keys[DIK_DIVIDE] = DIK_DIVIDE;
    keys[DIK_SYSRQ] = 84;
    keys[DIK_RMENU] = DIK_RMENU;
    keys[DIK_HOME] = DIK_HOME;
    keys[DIK_UP] = DIK_UP;
    keys[DIK_PRIOR] = DIK_PRIOR;
    keys[DIK_LEFT] = DIK_LEFT;
    keys[DIK_RIGHT] = DIK_RIGHT;
    keys[DIK_END] = DIK_END;
    keys[DIK_DOWN] = DIK_DOWN;
    keys[DIK_NEXT] = DIK_NEXT;
    keys[DIK_INSERT] = DIK_INSERT;
    keys[DIK_DELETE] = DIK_DELETE;
    keys[DIK_LWIN] = -1;
    keys[DIK_RWIN] = -1;
    keys[DIK_APPS] = -1;
}

// 0x4B43FC
void GNW95_hook_input(int hook)
{
    GNW95_hook_keyboard(hook);

    if (hook) {
        dxinput_acquire_mouse();
    } else {
        dxinput_unacquire_mouse();
    }
}

// 0x4C9C20
int GNW95_input_init()
{
    return 0;
}

// 0x4B446C
void GNW95_input_exit()
{
    GNW95_hook_keyboard(0);
}

// 0x4B4470
static int GNW95_hook_keyboard(int hook)
{
    // 0x539D7C
    static bool hooked = false;

    if (hook == hooked) {
        return 0;
    }

    if (!hook) {
        dxinput_unacquire_keyboard();

        UnhookWindowsHookEx(GNW95_keyboardHandle);

        kb_clear();

        hooked = hook;

        return 0;
    }

    if (dxinput_acquire_keyboard()) {
        GNW95_keyboardHandle = SetWindowsHookExA(WH_KEYBOARD, GNW95_keyboard_hook, 0, GetCurrentThreadId());
        kb_clear();
        hooked = hook;

        return 0;
    }

    return -1;
}

// 0x4B4494
LRESULT CALLBACK GNW95_keyboard_hook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) {
        if (wParam == VK_DELETE && lParam & 0x20000000 && GetAsyncKeyState(VK_CONTROL) & 0x80000000)
            return 0;

        if (wParam == VK_ESCAPE && GetAsyncKeyState(VK_CONTROL) & 0x80000000)
            return 0;

        if (wParam == VK_RETURN && lParam & 0x20000000)
            return 0;

        if (wParam == VK_NUMLOCK || wParam == VK_CAPITAL || wParam == VK_SCROLL) {
            // TODO: Get rid of this goto.
            goto next;
        }

        return 1;
    }

next:

    return CallNextHookEx(GNW95_keyboardHandle, nCode, wParam, lParam);
}

// 0x4B4538
void GNW95_process_message()
{
	int key;
	unsigned int now;
    MSG msg;

    if (GNW95_isActive && !kb_is_disabled()) {
        dxinput_key_data data;
        while (dxinput_read_keyboard_buffer(&data)) {
            GNW95_process_key(&data);
        }

        // NOTE: Uninline.
        now = get_time();

        for (key = 0; key < 256; key++) {
            GNW95RepeatStruct* ptr = &(GNW95_key_time_stamps[key]);
            if (ptr->time != -1) {
                unsigned int elapsedTime = ptr->time > now ? INT_MAX : now - ptr->time;
                unsigned int delay = ptr->count == 0 ? GNW95_repeat_delay : GNW95_repeat_rate;
                if (elapsedTime > delay) {
                    data.code = key;
                    data.state = 1;
                    GNW95_process_key(&data);

                    ptr->time = now;
                    ptr->count++;
                }
            }
        }
    }

    while (PeekMessageA(&msg, NULL, 0, 0, 0)) {
        if (GetMessageA(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
}

// 0x4B4638
void GNW95_clear_time_stamps()
{
	int index;

    for (index = 0; index < 256; index++) {
        GNW95_key_time_stamps[index].time = -1;
        GNW95_key_time_stamps[index].count = 0;
    }
}

// 0x4B465C
static void GNW95_process_key(dxinput_key_data* data)
{
    unsigned char flag = 0;
    unsigned char normalized_scan_code;

    switch (data->code) {
    case DIK_NUMPADENTER:
    case DIK_RCONTROL:
    case DIK_DIVIDE:
    case DIK_RMENU:
    case DIK_HOME:
    case DIK_UP:
    case DIK_PRIOR:
    case DIK_LEFT:
    case DIK_RIGHT:
    case DIK_END:
    case DIK_DOWN:
    case DIK_NEXT:
    case DIK_INSERT:
    case DIK_DELETE:
        flag = 1;
        break;
    }

    normalized_scan_code = GNW95_key_map[data->code];

    if (vcr_state == VCR_STATE_PLAYING) {
        if ((vcr_terminate_flags & VCR_TERMINATE_ON_KEY_PRESS) != 0) {
            vcr_terminated_condition = VCR_PLAYBACK_COMPLETION_REASON_TERMINATED;
            vcr_stop();
        }
    } else {
        if (flag) {
            kb_simulate_key(224);
            normalized_scan_code -= 0x80;
        }

        if (data->state == 1) {
            GNW95_key_time_stamps[data->code].time = get_time();
            GNW95_key_time_stamps[data->code].count = 0;
        } else {
            normalized_scan_code |= 0x80;
            GNW95_key_time_stamps[data->code].time = -1;
        }

        kb_simulate_key(normalized_scan_code);
    }
}

// 0x4B4734
void GNW95_lost_focus()
{
    if (focus_func != NULL) {
        focus_func(0);
    }

    while (!GNW95_isActive) {
        GNW95_process_message();

        if (idle_func != NULL) {
            idle_func();
        }
    }

    if (focus_func != NULL) {
        focus_func(1);
    }
}
