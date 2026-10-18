// Addresses inside GO VACATION main (build id 174471C5192F8647D804BD8C733C68D0), relative to main's base.
#pragma once
#pragma GCC visibility push(hidden)
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

extern "C" char __main_start[];      // provided by link.ld (module base - MAIN_DELTA)
extern "C" char __module_start[];
extern "C" char __module_end[];

template <typename T> static inline T game(uintptr_t off) { return reinterpret_cast<T>(__main_start + off); }
template <typename T> static inline T& game_var(uintptr_t off) { return *reinterpret_cast<T*>(__main_start + off); }

namespace gv {

// --- text rendering (used by the in-game debug pages, e.g. FUN_00caf00c) ---
inline void* text_writer()                      { return game<void* (*)()>(0x6F80)(); }
inline void  layer_begin(int layer)              { game<void (*)(int)>(0x1714)(layer); }
inline void  view_2d()                           { game<void (*)()>(0x17D4)(); }
inline void  text_begin()                        { game<void (*)()>(0x7C48)(); }
inline void  text_end()                          { game<void (*)()>(0x7C80)(); }
inline void  layer_end()                         { game<void (*)()>(0x1778)(); }
inline void  text_set(void* w, int a, int b, const char* s) { game<void (*)(void*, int, int, const char*, int)>(0xB99C)(w, a, b, s, 0); }
inline int   text_draw(float x, float y, float sx, float sy, float r, float g, float b, float a, void* w)
{
    using fn = int (*)(float, float, float, float, float, float, float, float, float, float, void*, int, int, int);
    return game<fn>(0xA67C)(x, y, sx, sy, r, g, b, a, 60.0f, 1.0f, w, 0, 0, 0x1B59);
}
// Draw without a message-table template: put the string into the writer's scratch buffer and call the inner
// renderer FUN_00008694 directly (FUN_0000a67c would first expand message 7001, which is not loaded during boot).
inline bool text_ready()
{
    void** msg_mgr = *(void***)(__main_start + 0x568A390);  // GOT -> message/font manager pointer
    return msg_mgr && *msg_mgr;
}
inline void text_draw_raw(float x, float y, float sx, float sy, float r, float g, float b, float a, void* w, const char* s)
{
    char* buf = *(char**)((char*)w + 0x21EA0);
    uintptr_t al = (uintptr_t)buf;
    if (al & 3) al += 4 - (al & 3);
    char* d = (char*)al;
    size_t i = 0;
    for (; s[i] && i < 0x3FF; i++) d[i] = s[i];
    d[i] = 0;
    using fn = int (*)(float, float, float, float, float, float, float, float, float, float, void*, uint64_t, uint32_t);
    game<fn>(0x8694)(x, y, sx, sy, r, g, b, a, 60.0f, 1.0f, w, 0, 0);
}

inline int vsnprintf(char* buf, size_t n, const char* fmt, va_list va)
{
    return game<int (*)(char*, size_t, const char*, va_list)>(0x1024D70)(buf, n, fmt, va);
}

// --- hooked functions ---
// build.py redirects every call of these functions to our hooks. The functions themselves stay untouched, so a
// hook calls the original simply at its address. Only DEBUG_PRINT is patched at its start: it just returns on
// Switch and is never called by us.
constexpr uintptr_t END_FRAME       = 0x534D3C;  // called once per frame right before present
constexpr uintptr_t DEBUG_PRINT     = 0xFBDEE0;  // Wii-era debug text (x, y, rgba, str) - stubbed to `ret` on Switch
constexpr uintptr_t SET_VIEW        = 0x04A7F0;  // camera object: set view matrix
constexpr uintptr_t GET_VIEW        = 0x04A858;  // camera object: get view matrix
constexpr uintptr_t SETUP_SOUND     = 0x8F3DB8;  // nn::atk SoundArchivePlayer::detail_SetupSoundImpl

// Code cave filled by build.py with one far jump into the plugin per hook (16 bytes each, slot order: see HOOKS
// in build.py). Ryujinx only applies IPS patches inside the segment, so we reuse dead code: the phase-0 handler
// of the BATTING engine's 'AVATAR TEST' debug map (only reachable from its jump table, never in normal play).
constexpr uintptr_t CAVE_BASE       = 0xCFB780;
constexpr uintptr_t hook_entry(int slot) { return CAVE_BASE + slot * 0x10; }

// --- camera ---
constexpr uintptr_t MAIN_CAMERA     = 0x59B2F20;  // first of 10 camera objects (0x160 bytes each)
// camera object: +0x70 eye (derived from the view matrix), +0xAC 3x4 view matrix, +0xF0 look-at target
constexpr uintptr_t MODULE_MGR      = 0x7C4D308; // module manager singleton; +0x30 = requested module, +0x34 = running module
inline int current_module()             { return game_var<int>(MODULE_MGR + 0x34); }

// --- player save data (FUN_00559e70) ---
constexpr uintptr_t PLAYER_DATA_PTR = 0x7C4C688;
constexpr uint32_t  PLAYER_SLOT_SIZE = 0x13160;
constexpr uint32_t  PLAYER_DATA_OFF  = 0x5E60;
constexpr uint32_t  ITEMS_OFF        = 0xC74;  // 46 bytes, bit0 = owned
constexpr uint32_t  FLAGS_OFF        = 0xCA4;  // 19 bytes, bit0 = unlocked, bits1-2 = "new" state
constexpr int       NUM_ITEMS = 46, NUM_FLAGS = 19;

inline uint8_t* player_data(int slot)
{
    uint8_t* base = game_var<uint8_t*>(PLAYER_DATA_PTR);
    return base ? base + (size_t)slot * PLAYER_SLOT_SIZE + PLAYER_DATA_OFF : nullptr;
}
inline int current_player() { uint32_t v = game<uint32_t (*)()>(0x55BF68)(); return (v & 0x80) ? -1 : (int)(int8_t)v; }

} // namespace gv
