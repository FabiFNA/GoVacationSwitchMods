// Player movement: teleport, saved positions and noclip flight.
// The player's position lives in a Havok hkpMotion (found by scanner::auto_start):
//   +0x50 transform translation, +0x60 centerOfMass0, +0x70 centerOfMass1 (w components are Havok bookkeeping).
#include "plugin.h"

namespace player {

uint64_t g_motion_pos;  // address of the translation vector (motion + 0x70 as found by the scanner)
bool g_noclip;
float g_fly[3];
float g_speed = 0.6f;
float g_saved[3][3];
bool g_saved_valid[3];


static float* com1() { return (float*)g_motion_pos; }
// still the same Havok motion object? (it is freed on scene changes)
bool valid()
{
    if (!g_motion_pos) return false;
    if (*(uint64_t*)(g_motion_pos - 0x70) != (uint64_t)__main_start + 0x5305A78) {
        logf("[gvmod] player object gone");
        g_motion_pos = 0; g_noclip = false;
        return false;
    }
    return true;
}

// a new module (activity or resort) frees the old player object: drop it and stop flying
void forget()
{
    g_motion_pos = 0;
    g_noclip = false;
    if (scanner::auto_busy()) scanner::g_auto_state = scanner::AUTO_IDLE;
}

// start a search if we need the player and do not know it
void ensure()
{
    if (!valid() && !scanner::auto_busy()) scanner::auto_start();
}

void get(float out[3]) { const float* p = com1(); out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; }

void set(const float p[3])
{
    if (!valid()) return;
    float* base = com1() - 8;           // motion + 0x50
    for (int v = 0; v < 3; v++)          // translation, centerOfMass0, centerOfMass1
        for (int i = 0; i < 3; i++) base[v * 4 + i] = p[i];
}

float g_check_cam[3], g_check_pos[3];
bool g_check_armed;

void attach(uint64_t pos_addr)
{
    g_motion_pos = pos_addr;
    g_check_armed = false;
    if (pos_addr) logf("[gvmod] player attached: %p", (void*)pos_addr);
}

// sanity check while playing normally: the game camera follows the player, so if the camera travelled
// far and our object stayed put, we latched onto a prop
static void follow_check()
{
    if (g_noclip || camera::g_mode != camera::CAM_GAME || !camera::g_have_view) { g_check_armed = false; return; }
    float e[3], p[3];
    camera::game_eye(e);
    get(p);
    if (!g_check_armed) { for (int i = 0; i < 3; i++) { g_check_cam[i] = e[i]; g_check_pos[i] = p[i]; } g_check_armed = true; return; }
    float dc = 0, dp = 0;
    for (int i = 0; i < 3; i += 2) { dc += (e[i] - g_check_cam[i]) * (e[i] - g_check_cam[i]); dp += (p[i] - g_check_pos[i]) * (p[i] - g_check_pos[i]); }
    if (dc > 25.0f) {  // camera moved 5+ units
        g_check_armed = false;
        if (dp < 1.0f) scanner::reject_pick();
    }
}

static float isqrt(float x)
{
    float r;
    __asm__("fsqrt %s0, %s1" : "=w"(r) : "w"(x));
    return r;
}

// called every frame with the raw controller state
void tick(uint64_t held, int lx, int ly, bool menu_open)
{
    if (!valid()) return;
    follow_check();
    scanner::verify_tick(lx, ly, !menu_open && !g_noclip);
    if (!g_noclip) return;
    if (!menu_open) {
        // camera-relative horizontal movement: forward = -(third row of the rendered view matrix)
        float fx = camera::g_have_view ? -camera::g_view[8] : 0, fz = camera::g_have_view ? -camera::g_view[10] : 1;
        float len = isqrt(fx * fx + fz * fz);
        if (len > 1e-4f) { fx /= len; fz /= len; } else { fx = 0; fz = 1; }
        float rx = -fz, rz = fx;  // right vector
        float sx = lx / 32767.0f, sy = ly / 32767.0f;
        if (sx > -0.15f && sx < 0.15f) sx = 0;
        if (sy > -0.15f && sy < 0.15f) sy = 0;
        float sp = g_speed * ((held & BTN_R) ? 4.0f : 1.0f);  // R = sprint
        g_fly[0] += (fx * sy + rx * sx) * sp;
        g_fly[2] += (fz * sy + rz * sx) * sp;
        if (held & BTN_ZR) g_fly[1] += sp;          // up
        if (held & BTN_L)  g_fly[1] -= sp;          // down
    }
    set(g_fly);
}

void toggle_noclip()
{
    if (!valid()) { ensure(); return; }
    g_noclip = !g_noclip;
    if (g_noclip) get(g_fly);
}

} // namespace player
