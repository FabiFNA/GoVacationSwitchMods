#pragma once
#include "game.h"

struct NpadState {
    uint64_t sampling;
    uint64_t buttons;
    int32_t lx, ly, rx, ry;
    uint32_t attributes;  // bit0 = connected
    uint32_t reserved;
};

enum Button : uint64_t {
    BTN_A = 1 << 0, BTN_B = 1 << 1, BTN_X = 1 << 2, BTN_Y = 1 << 3, BTN_LS = 1 << 4, BTN_RS = 1 << 5,
    BTN_L = 1 << 6, BTN_R = 1 << 7, BTN_ZL = 1 << 8, BTN_ZR = 1 << 9, BTN_PLUS = 1 << 10, BTN_MINUS = 1 << 11,
    BTN_LEFT = 1 << 12, BTN_UP = 1 << 13, BTN_RIGHT = 1 << 14, BTN_DOWN = 1 << 15,
};

// hid.cpp
struct Input { uint64_t buttons; int lx, ly, rx, ry; };
Input read_input();
extern Input g_inject;
// menu.cpp
bool menu_wants_input();
void log_line(const char* s);
void logf(const char* fmt, ...);

// scanner.cpp
namespace scanner {
struct Cand { uint64_t addr; float x, z; };
extern Cand g_cands[];
extern int g_count, g_undo_count;
extern bool g_overflow;
extern int g_round;
void start();
void filter(bool want_changed);
void undo();
void lift_half(bool first, float dy);
void keep_half(bool first);
enum AutoState { AUTO_IDLE, AUTO_COLLECT, AUTO_RUNNING, AUTO_PTRSCAN, AUTO_FOUND, AUTO_FAILED };
extern AutoState g_auto_state;
extern int g_auto_count, g_auto_index;
extern uint64_t g_auto_found;
void auto_start();
void auto_tick();
bool auto_busy();
void reject_pick();
void verify_tick(int lx, int ly, bool active);
extern bool g_confirmed;
}

// player.cpp
namespace player {
extern bool g_noclip;
extern float g_speed;
extern float g_saved[3][3];
extern bool g_saved_valid[3];
bool valid();
void get(float out[3]);
void set(const float p[3]);
void attach(uint64_t pos_addr);
void tick(uint64_t held, int lx, int ly, bool menu_open);
void toggle_noclip();
void ensure();
void forget();
}

// camera.cpp
namespace camera {
enum Mode { CAM_GAME, CAM_ORBIT, CAM_FREE };
extern Mode g_mode;
extern float g_dist, g_fov_speed;
extern bool g_free_invert_y;
void set_mode(Mode m);
void tick(const Input& in, bool menu_open);
void diag_frame(uint32_t frame);
extern float g_view[12], g_game_view[12];
extern bool g_have_view;
extern unsigned g_view_count;
void game_eye(float e[3]);
}

// devmenu.cpp
namespace devmenu {
extern bool g_active;
bool open();
void close();
void tick();
}

// radio.cpp
namespace radio {
constexpr int SONG_COUNT = 20;
extern const char* const SONGS[SONG_COUNT];
extern bool g_enabled;
extern int g_song, g_playing;
void play_now();
}
