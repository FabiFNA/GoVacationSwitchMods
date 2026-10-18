// Differential memory scanner for finding the player position (x/z floats that change while walking).
// Candidates live in our own bss; results are also written to the Ryujinx log via svcOutputDebugString.
#include "plugin.h"

namespace scanner {

struct MemoryInfo { uint64_t addr, size; uint32_t type, attr, perm, ipc, device, pad; };

constexpr int MAX_CANDS = 2 << 20;   // 32 MiB
constexpr int MAX_UNDO = 1 << 16;
Cand g_cands[MAX_CANDS];
Cand g_undo[MAX_UNDO];
int g_count, g_undo_count = -1;
bool g_overflow;
int g_round;

static int query(MemoryInfo* mi, uint64_t addr)
{
    uint32_t page;
    register uint64_t x0 __asm__("x0") = (uint64_t)mi;
    register uint64_t x1 __asm__("x1") = (uint64_t)&page;
    register uint64_t x2 __asm__("x2") = addr;
    __asm__ volatile("svc 0x6" : "+r"(x0), "+r"(x1), "+r"(x2) :: "memory");  // svcQueryMemory
    return (int)x0;
}

static bool plausible(float x, float y, float z)
{
    auto fin = [](float v) { return v == v && v > -8000.f && v < 8000.f; };
    if (!fin(x) || !fin(y) || !fin(z)) return false;
    if (y < -100.f || y > 1500.f) return false;
    auto big = [](float v) { return v > 2.f || v < -2.f; };
    return big(x) && big(z);
}

// stricter test for the first scan: world positions are practically never whole numbers
static bool plausible_first(float x, float y, float z)
{
    return plausible(x, y, z) && !(x == (float)(int)x && z == (float)(int)z);
}

static bool own_memory(uint64_t a)
{
    return (a >= (uint64_t)g_cands && a < (uint64_t)(g_cands + MAX_CANDS)) ||
           (a >= (uint64_t)g_undo && a < (uint64_t)(g_undo + MAX_UNDO));
}

static void save_undo()
{
    if (g_count > MAX_UNDO) { g_undo_count = -1; return; }
    for (int i = 0; i < g_count; i++) g_undo[i] = g_cands[i];
    g_undo_count = g_count;
}

static void dump()
{
    if (g_count > 300) return;
    for (int i = 0; i < g_count; i++) {
        const float* f = (const float*)g_cands[i].addr;
        logf("[gvmod]   cand %3d %p  %.3f %.3f %.3f", i, (void*)g_cands[i].addr, f[0], f[1], f[2]);
    }
}

void start()
{
    g_count = 0; g_overflow = false; g_round = 1; g_undo_count = -1;
    MemoryInfo mi;
    uint64_t addr = 0, scanned = 0;
    while (query(&mi, addr) == 0) {
        // RW memory: CodeMutable (static data), Heap, ModuleCodeMutable
        if (mi.perm == 3 && (mi.type == 4 || mi.type == 5 || mi.type == 9) && !(mi.attr & 0xC) && !own_memory(mi.addr)) {
            auto* f = (const float*)mi.addr;
            uint64_t n = mi.size / 4;
            scanned += mi.size;
            for (uint64_t i = 0; i + 2 < n && !g_overflow; i++) {
                if (!plausible_first(f[i], f[i + 1], f[i + 2])) continue;
                uint64_t a = (uint64_t)&f[i];
                if (own_memory(a)) continue;
                if (g_count >= MAX_CANDS) { g_overflow = true; break; }
                g_cands[g_count++] = {a, f[i], f[i + 2]};
            }
        }
        uint64_t next = mi.addr + mi.size;
        if (next <= addr) break;
        addr = next;
    }
    logf("[gvmod] scan: %d candidates%s, %llu MiB scanned", g_count, g_overflow ? " (OVERFLOW)" : "",
         (unsigned long long)(scanned >> 20));
}

// keep = changed (moved) or unchanged (standing still)
void filter(bool want_changed)
{
    save_undo();
    int out = 0;
    for (int i = 0; i < g_count; i++) {
        Cand c = g_cands[i];
        const float* f = (const float*)c.addr;
        float x = f[0], y = f[1], z = f[2];
        if (!plausible(x, y, z)) continue;
        float dx = x - c.x, dz = z - c.z;
        float d2 = dx * dx + dz * dz;
        bool keep = want_changed ? d2 > 0.25f : d2 < 1e-4f;
        if (keep) g_cands[out++] = {c.addr, x, z};
    }
    g_count = out;
    g_round++;
    logf("[gvmod] filter(%s): %d candidates", want_changed ? "moved" : "still", g_count);
    dump();
}

void undo()
{
    if (g_undo_count < 0) return;
    for (int i = 0; i < g_undo_count; i++) g_cands[i] = g_undo[i];
    g_count = g_undo_count;
    g_undo_count = -1;
    logf("[gvmod] undo: %d candidates", g_count);
}

// bisection: lift one half; keep the half that actually moved the character
void lift_half(bool first, float dy)
{
    int h = g_count / 2;
    int lo = first ? 0 : h, hi = first ? h : g_count;
    for (int i = lo; i < hi; i++) ((float*)g_cands[i].addr)[0] += dy;  // shift x: y snaps back to the ground
}

void keep_half(bool first)
{
    save_undo();
    int h = g_count / 2;
    if (first) g_count = h;
    else { for (int i = h; i < g_count; i++) g_cands[i - h] = g_cands[i]; g_count -= h; }
    for (int i = 0; i < g_count; i++) {  // refresh snapshot so later moved/still filters stay valid
        const float* f = (const float*)g_cands[i].addr;
        g_cands[i].x = f[0]; g_cands[i].z = f[2];
    }
    logf("[gvmod] keep %s half: %d candidates", first ? "first" : "second", g_count);
    dump();
}

} // namespace scanner

// ------------------------------------------------------------------------------------------------------------
// Automatic search for the authoritative player position (spread over several frames to keep the game smooth).
// main+0x7CB5800 holds a per-frame copy of the player position (inside an arena buffer). We collect every float
// triple in memory that matches it, then nudge each candidate by +2 on x and watch whether the copy follows.
// Afterwards a pointer scan logs every word that points up to 0x2000 bytes before the found position.
namespace scanner {

constexpr uintptr_t REF_POS = 0x7CB5800;
constexpr int AUTO_MAX = 4096, SETTLE_FRAMES = 12, PTR_MAX = 256;
constexpr uint64_t BYTES_PER_FRAME = 6ull << 20;
uint64_t g_auto[AUTO_MAX];
int g_auto_count, g_auto_index, g_auto_wait;
AutoState g_auto_state = AUTO_IDLE;
uint64_t g_auto_found;
float g_ref_before, g_ref_x, g_ref_z;
uint64_t g_ptrs[PTR_MAX];
int g_ptr_count;

// incremental walk over readable+writable game memory
struct Walk { uint64_t addr, off; MemoryInfo mi; bool valid, done, single; };  // single: stop after the first region
Walk g_walk;

static void walk_reset() { g_walk = {}; }

// calls fn(ptr, bytes) for up to `budget` bytes; returns false when the whole address space was visited
template <typename F> static bool walk_step(uint64_t budget, F fn)
{
    Walk& w = g_walk;
    while (budget && !w.done) {
        if (!w.valid) {
            if (query(&w.mi, w.addr) != 0) { w.done = true; break; }
            w.valid = true; w.off = 0;
            // skip DeviceShared/Uncached memory (GPU): CPU reads there make Ryujinx drop resolution scaling
            bool use = w.mi.perm == 3 && w.mi.type != 0 && w.mi.type != 1 && w.mi.type != 6 && !(w.mi.attr & 0xC) && !own_memory(w.mi.addr) &&
                       !(w.mi.addr >= (uint64_t)g_auto && w.mi.addr < (uint64_t)(g_auto + AUTO_MAX));
            if (!use) w.off = w.mi.size;
        }
        uint64_t n = w.mi.size - w.off;
        if (n > budget) n = budget;
        if (n) { fn(w.mi.addr + w.off, n); w.off += n; budget -= n; }
        if (w.off >= w.mi.size) {
            uint64_t next = w.mi.addr + w.mi.size;
            if (next <= w.addr || w.single) w.done = true;
            w.addr = next; w.valid = false;
        }
    }
    return !w.done;
}

static const float* ref() { return (const float*)(__main_start + REF_POS); }

// the player's Havok motion object starts with this vtable; its position (centerOfMass1) is at +0x70
constexpr uintptr_t MOTION_VTABLE = 0x5305A78;

uint64_t g_last_region;  // base of the memory region that held the player last time
int g_auto_pick = -1;
bool g_rejected[AUTO_MAX];
extern bool g_confirmed;
static void verify_reset();

static uint64_t region_of(uint64_t a)
{
    MemoryInfo mi;
    return query(&mi, a) == 0 ? mi.addr : 0;
}

// Scanning all memory also reads GPU render targets, which makes Ryujinx pull them back to the CPU and drop their
// resolution scaling. The player's Havok objects live in one heap region (always around 0x2131xxxxxx so far),
// so we only search that region and fall back to a full scan if it holds no motion objects.
constexpr uint64_t HAVOK_REGION_HINT = 0x2131F00000;
bool g_full_scan;

void auto_start()
{
    g_auto_count = 0; g_auto_found = 0; g_ptr_count = 0; g_auto_pick = -1;
    for (bool& r : g_rejected) r = false;
    walk_reset();
    g_walk.addr = g_last_region ? g_last_region : HAVOK_REGION_HINT;
    g_walk.single = true;
    g_full_scan = false;
    g_auto_state = AUTO_COLLECT;
}

bool auto_busy() { return g_auto_state == AUTO_COLLECT || g_auto_state == AUTO_RUNNING || g_auto_state == AUTO_PTRSCAN; }

// The player's body stays upright: its orientation quaternion (motion+0x90 = pos+0x20) is identity.
// Balls and other props share the motion class but roll around.
static bool upright(uint64_t pos)
{
    const float* q = (const float*)(pos + 0x20);
    return q[0] > -0.02f && q[0] < 0.02f && q[2] > -0.02f && q[2] < 0.02f && (q[3] > 0.99f || q[3] < -0.99f);
}

// pick the upright motion closest to the game camera's view ray (the third-person camera looks at the player)
static bool pick_best()
{
    float e[3]; camera::game_eye(e);
    const float* b = &camera::g_game_view[8];  // back axis; forward = -b
    int best = -1; float best_d = 16.0f;         // max squared distance from the ray (4 units)
    for (int i = 0; i < g_auto_count; i++) {
        if (g_rejected[i] || !upright(g_auto[i])) continue;
        if (*(uint64_t*)(g_auto[i] - 0x70) != (uint64_t)__main_start + MOTION_VTABLE) continue;
        const float* f = (const float*)g_auto[i];
        float v[3] = {f[0] - e[0], f[1] - e[1], f[2] - e[2]};
        float along = -(v[0] * b[0] + v[1] * b[1] + v[2] * b[2]);
        if (along < 1.0f || along > 25.0f) continue;
        float px = v[0] + b[0] * along, py = v[1] + b[1] * along, pz = v[2] + b[2] * along;
        float d = px * px + py * py + pz * pz;
        if (d < best_d) { best_d = d; best = i; }
    }
    if (best < 0) return false;
    g_auto_found = g_auto[best];
    g_auto_pick = best;
    g_confirmed = false;
    verify_reset();
    logf("[gvmod] auto: picked motion %d (ray dist %.2f)", best, best_d);
    player::attach(g_auto_found);
    return true;
}

// the camera moved a lot but our pick did not: it was a prop, take the next candidate
void reject_pick()
{
    if (g_auto_pick < 0) return;
    g_rejected[g_auto_pick] = true;
    logf("[gvmod] auto: motion %d does not follow the camera, re-picking", g_auto_pick);
    if (!pick_best()) { player::attach(0); auto_start(); }
}

// Continuous check against the left stick: only the player walks where the stick points (relative to the game
// camera). NPCs share the motion class, stand upright and can end up closest to the camera ray, so the initial
// pick may be one of them. While the player walks we score every upright motion object by how well its movement
// matches the stick and switch to the clear winner.
constexpr int VERIFY_FRAMES = 40;
static float fsqrt(float x) { float r; __asm__("fsqrt %s0, %s1" : "=w"(r) : "w"(x)); return r; }
float g_prev[AUTO_MAX][3];
float g_score[AUTO_MAX];
bool g_prev_ok[AUTO_MAX];
int g_evidence;
bool g_confirmed;

static bool motion_alive(int i) { return *(uint64_t*)(g_auto[i] - 0x70) == (uint64_t)__main_start + MOTION_VTABLE; }

static void verify_reset()
{
    g_evidence = 0;
    for (int i = 0; i < g_auto_count; i++) { g_score[i] = 0; g_prev_ok[i] = false; }
}

void verify_tick(int lx, int ly, bool active)
{
    if (g_auto_state != AUTO_FOUND || !camera::g_have_view) return;
    float sx = lx / 32767.0f, sy = ly / 32767.0f;
    // only with the game's own camera: stick directions are relative to it
    bool walking = active && camera::g_mode == camera::CAM_GAME && sx * sx + sy * sy > 0.36f;
    // expected direction on the ground, same mapping as noclip flight
    const float* b = &camera::g_game_view[8];
    float fx = -b[0], fz = -b[2];
    float fl = fx * fx + fz * fz;
    float ex = 0, ez = 0;
    if (walking && fl > 1e-4f) {
        float rx = -fz, rz = fx;
        ex = fx * sy + rx * sx; ez = fz * sy + rz * sx;
        float el = ex * ex + ez * ez;
        if (el < 1e-6f) walking = false;
        else { float inv = 1.0f / fsqrt(el); ex *= inv; ez *= inv; }
    } else walking = false;

    for (int i = 0; i < g_auto_count; i++) {
        if (g_rejected[i] || !motion_alive(i)) { g_prev_ok[i] = false; continue; }
        const float* f = (const float*)g_auto[i];
        if (walking && g_prev_ok[i]) {
            float dx = f[0] - g_prev[i][0], dz = f[2] - g_prev[i][2];
            float d2 = dx * dx + dz * dz;
            if (d2 > 1e-5f && d2 < 4.0f && upright(g_auto[i])) g_score[i] += (dx * ex + dz * ez) / fsqrt(d2);
            else g_score[i] -= 0.5f;  // standing still (or teleporting) while the stick is held
        }
        g_prev[i][0] = f[0]; g_prev[i][1] = f[1]; g_prev[i][2] = f[2];
        g_prev_ok[i] = true;
    }
    if (!walking || ++g_evidence < VERIFY_FRAMES) return;

    // Be conservative: keep a pick that follows the stick reasonably well, and only switch away from one that
    // clearly does not when exactly one other object clearly does (a companion walking beside the player also
    // scores well, so a close race never switches).
    float n = (float)g_evidence;
    float ps = g_auto_pick >= 0 ? g_score[g_auto_pick] : -n;
    int best = -1, second = -1;
    for (int i = 0; i < g_auto_count; i++) {
        if (i == g_auto_pick) continue;
        if (best < 0 || g_score[i] > g_score[best]) { second = best; best = i; }
        else if (second < 0 || g_score[i] > g_score[second]) second = i;
    }
    float bs = best >= 0 ? g_score[best] : -n, ss = second >= 0 ? g_score[second] : -n;
    logf("[gvmod] verify: pick %d %.1f, best %d %.1f, second %d %.1f (of %d)", g_auto_pick, ps, best, bs, second, ss, g_evidence);
    if (ps >= 0.5f * n) g_confirmed = true;
    else if (ps < 0.0f && bs >= 0.8f * n && ss < 0.4f * n) {
        float e[3]; camera::game_eye(e);
        const float* f = (const float*)g_auto[best];
        float dx = f[0] - e[0], dy = f[1] - e[1], dz = f[2] - e[2];
        if (dx * dx + dy * dy + dz * dz < 400.0f) {  // within 20 units of the game camera
            logf("[gvmod] verify: switching to motion %d", best);
            g_auto_pick = best;
            g_auto_found = g_auto[best];
            player::attach(g_auto_found);
            g_confirmed = true;
        }
    }
    verify_reset();
}

static void collect(uint64_t base, uint64_t n)
{
    const uint64_t* w = (const uint64_t*)base;
    uint64_t vt = (uint64_t)__main_start + MOTION_VTABLE;
    for (uint64_t i = 0; i < n / 8 && g_auto_count < AUTO_MAX; i++)
        if (w[i] == vt) g_auto[g_auto_count++] = (uint64_t)&w[i] + 0x70;
}

// camera eye x from the game's own view matrix (the game camera follows the player)
static float cam_x() { float e[3]; camera::game_eye(e); return e[0]; }

static void collect_ptrs(uint64_t base, uint64_t n)
{
    const uint64_t* w = (const uint64_t*)base;
    uint64_t lo = g_auto_found - 0x2000, hi = g_auto_found;
    for (uint64_t i = 0; i < n / 8 && g_ptr_count < PTR_MAX; i++)
        if (w[i] >= lo && w[i] <= hi) g_ptrs[g_ptr_count++] = (uint64_t)&w[i];
}

static void dump_around(uint64_t a)
{
    logf("[gvmod] player position master at %p", (void*)a);
    const uint64_t* q = (const uint64_t*)((a & ~7ull) - 0x100);
    for (int i = 0; i < 0x40; i++) {
        uint64_t v = q[i];
        const char* tag = (v >= (uint64_t)__main_start && v < (uint64_t)__main_start + 0x7D6B000) ? "  <- main" : "";
        logf("[gvmod]   %p: %016llx%s", (void*)&q[i], (unsigned long long)v, tag);
    }
}

// called once per frame
void auto_tick()
{
    switch (g_auto_state) {
    case AUTO_COLLECT:
        if (walk_step(BYTES_PER_FRAME, collect)) return;
        if (!g_auto_count && !g_full_scan) {  // nothing in the expected region: search everything
            log_line("[gvmod] auto: hint region empty, full scan");
            g_full_scan = true; g_last_region = 0; walk_reset(); return;
        }
        {
            MemoryInfo mi;
            if (g_auto_count && query(&mi, g_auto[0]) == 0)
                logf("[gvmod] auto: region %p size %llx type %x attr %x", (void*)mi.addr, (unsigned long long)mi.size, mi.type, mi.attr);
        }
        logf("[gvmod] auto: %d motion objects", g_auto_count);
        for (int i = 0; i < g_auto_count && i < 64; i++) {
            const float* f = (const float*)g_auto[i];
            logf("[gvmod]   motion %2d %p  %.2f %.2f %.2f", i, (void*)g_auto[i], f[0], f[1], f[2]);
        }
        if (pick_best()) {
            g_last_region = region_of(g_auto_found);
            g_auto_state = AUTO_FOUND;
            return;
        }
        g_auto_index = 0; g_auto_wait = 0;
        g_auto_state = g_auto_count ? AUTO_RUNNING : AUTO_FAILED;
        return;
    case AUTO_RUNNING:
        if (g_auto_wait == 0) {
            if (g_auto_index >= g_auto_count) { g_auto_state = AUTO_FAILED; log_line("[gvmod] auto: nothing moved the player"); return; }
            g_ref_before = cam_x();
            ((float*)g_auto[g_auto_index])[0] += 2.0f;
            g_auto_wait = SETTLE_FRAMES;
            return;
        }
        if (--g_auto_wait > 0) return;
        {
            float moved = cam_x() - g_ref_before;
            if (moved > 0.5f && moved < 4.0f) {
                g_auto_found = g_auto[g_auto_index];
                g_auto_pick = g_auto_index;
                g_confirmed = false;
                verify_reset();
                ((float*)g_auto_found)[0] -= 2.0f;   // put the player back
                g_last_region = region_of(g_auto_found);
                player::attach(g_auto_found);
                g_auto_state = AUTO_FOUND;
                return;
            }
        }
        g_auto_index++;
        return;
    case AUTO_PTRSCAN:
        if (walk_step(BYTES_PER_FRAME, collect_ptrs)) return;
        logf("[gvmod] ptrscan: %d refs to [pos-0x2000, pos]", g_ptr_count);
        for (int i = 0; i < g_ptr_count; i++) {
            uint64_t at = g_ptrs[i], v = *(uint64_t*)at;
            bool st = at >= (uint64_t)__main_start && at < (uint64_t)__main_start + 0x7D6B000;
            logf("[gvmod]   ref %p%s -> %p (pos-%llx)", (void*)at, st ? " (MAIN STATIC)" : "", (void*)v,
                 (unsigned long long)(g_auto_found - v));
        }
        g_auto_state = AUTO_FOUND;
        return;
    default:
        return;
    }
}

} // namespace scanner
