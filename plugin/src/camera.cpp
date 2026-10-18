// Camera control: orbit camera around the player (right stick) and detached free camera.
// We hook FUN_0004a7f0(camera, viewMtx) - the setter that stores a camera's 3x4 view matrix - and substitute our own
// look-at matrix for the main camera.
#include "plugin.h"

namespace fm {
constexpr float PI = 3.14159265f;
float sqrt(float x) { float r; __asm__("fsqrt %s0, %s1" : "=w"(r) : "w"(x)); return r; }
float wrap(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
float sin(float x)
{
    x = wrap(x);
    // sin on [-pi, pi] via reduction to [-pi/2, pi/2] and a 7th order polynomial
    if (x > PI / 2) x = PI - x; else if (x < -PI / 2) x = -PI - x;
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42)));
}
float cos(float x) { return sin(x + PI / 2); }
float atan(float x)  // |x| <= 1
{
    float x2 = x * x;
    return x * (0.99997726f + x2 * (-0.33262347f + x2 * (0.19354346f + x2 * (-0.11643287f + x2 * (0.05265332f - x2 * 0.01172120f)))));
}
float atan2(float y, float x)
{
    if (x == 0 && y == 0) return 0;
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    float a = ax > ay ? atan(ay / ax) : PI / 2 - atan(ax / ay);
    if (x < 0) a = PI - a;
    return y < 0 ? -a : a;
}
} // namespace fm

namespace camera {

Mode g_mode = CAM_GAME;
float g_yaw, g_pitch = 0.3f, g_dist = 6.0f;     // orbit
float g_pos[3];                                   // free camera position
float g_fov_speed = 0.35f;
bool g_free_invert_y;
bool g_logged;

float g_game_view[12];   // last view matrix the game produced for the main camera
float g_view[12];        // last view matrix actually handed to the renderer (ours or the game's)
bool g_have_view;

// camera position of a 3x4 view matrix: eye = -R^T * t
static void view_eye(const float* m, float e[3])
{
    for (int j = 0; j < 3; j++) e[j] = -(m[0 + j] * m[3] + m[4 + j] * m[7] + m[8 + j] * m[11]);
}

void game_eye(float e[3]) { view_eye(g_game_view, e); }

// orbit pivot: the player's Havok position; before the player is found, a point 6 units in front of the game camera
static void pivot(float out[3])
{
    if (player::valid()) { player::get(out); out[1] += 1.6f; return; }
    float e[3]; view_eye(g_game_view, e);
    for (int i = 0; i < 3; i++) out[i] = e[i] - g_game_view[8 + i] * 6.0f;
}

// direction from the pivot towards the eye for the given yaw/pitch
static void dir(float yaw, float pitch, float d[3])
{
    d[0] = fm::cos(pitch) * fm::sin(yaw);
    d[1] = fm::sin(pitch);
    d[2] = fm::cos(pitch) * fm::cos(yaw);
}

void set_mode(Mode m)
{
    if (m == g_mode) return;
    if (m == CAM_ORBIT) player::ensure();
    // derive angles from the game's current view matrix so the switch is seamless
    if (g_have_view) {
        const float* b = &g_game_view[8];  // third row = "back" axis (from the look target towards the eye)
        g_yaw = fm::atan2(b[0], b[2]);
        g_pitch = fm::atan2(b[1], fm::sqrt(b[0] * b[0] + b[2] * b[2]));
        float e[3], pv[3];
        view_eye(g_game_view, e);
        pivot(pv);
        float dx = e[0] - pv[0], dy = e[1] - pv[1], dz = e[2] - pv[2];
        float len = fm::sqrt(dx * dx + dy * dy + dz * dz);
        if (m == CAM_ORBIT) g_dist = (len < 2 || len > 40) ? 6 : len;
        if (m == CAM_FREE) { g_pos[0] = e[0]; g_pos[1] = e[1]; g_pos[2] = e[2]; }
    }
    g_mode = m;
    g_logged = false;
}

static float axis(int v) { float f = v / 32767.0f; return (f > -0.15f && f < 0.15f) ? 0 : f; }

// per frame, from the end-of-frame hook
void tick(const Input& in, bool menu_open)
{
    if (g_mode == CAM_GAME || menu_open) return;
    float rx = axis(in.rx), ry = axis(in.ry);
    g_yaw = fm::wrap(g_yaw - rx * 0.045f);
    g_pitch += (g_mode == CAM_FREE && g_free_invert_y ? ry : -ry) * 0.035f;
    if (g_pitch > 1.45f) g_pitch = 1.45f;
    if (g_pitch < -1.45f) g_pitch = -1.45f;

    if (g_mode == CAM_ORBIT) {
        if (in.buttons & BTN_UP)   g_dist -= 0.15f;
        if (in.buttons & BTN_DOWN) g_dist += 0.15f;
        if (g_dist < 1.5f) g_dist = 1.5f;
        if (g_dist > 60) g_dist = 60;
    } else {
        // free camera: forward is from eye towards the look direction (opposite of dir())
        float d[3]; dir(g_yaw, g_pitch, d);
        float fx = -d[0], fy = -d[1], fz = -d[2];
        float rgx = fz, rgz = -fx;  // right vector on the ground plane
        float rl = fm::sqrt(rgx * rgx + rgz * rgz); if (rl > 1e-4f) { rgx /= rl; rgz /= rl; }
        float lx = axis(in.lx), ly = axis(in.ly);
        float sp = g_fov_speed * ((in.buttons & BTN_R) ? 4.0f : 1.0f);
        g_pos[0] += (fx * ly - rgx * lx) * sp;
        g_pos[1] += fy * ly * sp;
        g_pos[2] += (fz * ly - rgz * lx) * sp;
        if (in.buttons & BTN_ZR) g_pos[1] += sp;
        if (in.buttons & BTN_L)  g_pos[1] -= sp;
    }
}

// Wii MTXLookAt layout: rows = right, up, back(+z towards the viewer); translation = -dot(axis, eye)
static void look_at(float m[12], const float eye[3], const float tgt[3])
{
    float l[3] = {eye[0] - tgt[0], eye[1] - tgt[1], eye[2] - tgt[2]};
    float n = fm::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]); if (n < 1e-5f) n = 1;
    for (float& v : l) v /= n;
    float r[3] = {l[2], 0, -l[0]};  // cross((0,1,0), l)
    n = fm::sqrt(r[0] * r[0] + r[2] * r[2]); if (n < 1e-5f) n = 1;
    r[0] /= n; r[2] /= n;
    float u[3] = {l[1] * r[2] - l[2] * r[1], l[2] * r[0] - l[0] * r[2], l[0] * r[1] - l[1] * r[0]};
    const float* ax[3] = {r, u, l};
    for (int i = 0; i < 3; i++) {
        m[i * 4 + 0] = ax[i][0]; m[i * 4 + 1] = ax[i][1]; m[i * 4 + 2] = ax[i][2];
        m[i * 4 + 3] = -(ax[i][0] * eye[0] + ax[i][1] * eye[1] + ax[i][2] * eye[2]);
    }
}

} // namespace camera

namespace camera {
void* g_seen[8];
int g_seen_count, g_calls;
void* g_gseen[8];
int g_gseen_count, g_gcalls;
unsigned g_view_count;  // calls of the main renderer's camera, see hk_end_frame
void diag_frame(uint32_t frame)
{
    if (frame % 600 == 0) {
        logf("[gvmod] setview: %d calls in last 600 frames, %d cams:", g_calls, g_seen_count);
        for (int i = 0; i < g_seen_count; i++)
            logf("[gvmod]   cam %p (main+%llx)", g_seen[i], (unsigned long long)((char*)g_seen[i] - __main_start));
        g_calls = 0;
        logf("[gvmod] getview: %d calls, %d cams:", g_gcalls, g_gseen_count);
        for (int i = 0; i < g_gseen_count; i++)
            logf("[gvmod]   gcam %p (main+%llx)", g_gseen[i], (unsigned long long)((char*)g_gseen[i] - __main_start));
        g_gcalls = 0;
    }
}
}

namespace camera {
// our view matrix for the current mode
static void build(float m[12])
{
    float eye[3], tgt[3], d[3];
    dir(g_yaw, g_pitch, d);
    if (g_mode == CAM_ORBIT) {
        pivot(tgt);
        for (int i = 0; i < 3; i++) eye[i] = tgt[i] + d[i] * g_dist;
    } else {
        for (int i = 0; i < 3; i++) { eye[i] = g_pos[i]; tgt[i] = g_pos[i] - d[i]; }
    }
    look_at(m, eye, tgt);
}
}

// FUN_0004a858(camera, out): hands a camera's view matrix to the renderer
extern "C" __attribute__((visibility("hidden"))) void hk_get_view(void* cam, float* out)
{
    using namespace camera;
    g_gcalls++;
    g_view_count++;
    bool seen = false;
    for (int i = 0; i < g_gseen_count; i++) seen |= g_gseen[i] == cam;
    if (!seen && g_gseen_count < 8) g_gseen[g_gseen_count++] = cam;
    game<void (*)(void*, float*)>(gv::GET_VIEW)(cam, out);
    if (cam != (void*)(__main_start + gv::MAIN_CAMERA)) return;
    for (int i = 0; i < 12; i++) g_game_view[i] = out[i];
    g_have_view = true;
    if (g_mode == CAM_GAME) { for (int i = 0; i < 12; i++) g_view[i] = out[i]; return; }
    float m[12];
    build(m);
    for (int i = 0; i < 12; i++) g_view[i] = m[i];
    if (!g_logged) {
        g_logged = true;
        logf("[gvmod] game view  %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
             out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], out[8], out[9], out[10], out[11]);
        logf("[gvmod] our  view  %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
             m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
    }
    for (int i = 0; i < 12; i++) out[i] = m[i];
}

extern "C" __attribute__((visibility("hidden"))) void hk_set_view(void* cam, float* mtx)
{
    using namespace camera;
    g_calls++;
    bool seen = false;
    for (int i = 0; i < g_seen_count; i++) seen |= g_seen[i] == cam;
    if (!seen && g_seen_count < 8) g_seen[g_seen_count++] = cam;
    auto orig = game<void (*)(void*, float*)>(gv::SET_VIEW);
    if (g_mode == CAM_GAME || cam != (void*)(__main_start + gv::MAIN_CAMERA)) { orig(cam, mtx); return; }

    float eye[3], tgt[3], d[3];
    dir(g_yaw, g_pitch, d);
    if (g_mode == CAM_ORBIT) {
        pivot(tgt);
        for (int i = 0; i < 3; i++) eye[i] = tgt[i] + d[i] * g_dist;
    } else {
        for (int i = 0; i < 3; i++) { eye[i] = g_pos[i]; tgt[i] = g_pos[i] - d[i]; }
    }
    float m[12];
    look_at(m, eye, tgt);
    if (false) {
        g_logged = true;
        logf("[gvmod] game view  %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
             mtx[0], mtx[1], mtx[2], mtx[3], mtx[4], mtx[5], mtx[6], mtx[7], mtx[8], mtx[9], mtx[10], mtx[11]);
        logf("[gvmod] our  view  %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
             m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
    }
    orig(cam, m);
}
