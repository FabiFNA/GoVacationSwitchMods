// GO VACATION mod menu: overlay drawn with the game's own debug text renderer at the end of every frame.
#include "plugin.h"

// ---------------------------------------------------------------- freestanding helpers
extern "C" {
__attribute__((visibility("hidden"))) void* memset(void* d, int c, size_t n) { auto* p = (unsigned char*)d; while (n--) *p++ = (unsigned char)c; return d; }
__attribute__((visibility("hidden"))) void* memcpy(void* d, const void* s, size_t n) { auto* a = (char*)d; auto* b = (const char*)s; while (n--) *a++ = *b++; return d; }
__attribute__((visibility("hidden"))) size_t strlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
}

static void fmt(char* buf, size_t n, const char* f, ...)
{
    va_list va; va_start(va, f); gv::vsnprintf(buf, n, f, va); va_end(va);
}

void log_line(const char* s)
{
    register const char* x0 __asm__("x0") = s;
    register size_t x1 __asm__("x1") = strlen(s);
    __asm__ volatile("svc 0x27" : "+r"(x0), "+r"(x1) :: "memory");  // svcOutputDebugString -> Ryujinx log
}

void logf(const char* f, ...)
{
    char buf[256];
    va_list va; va_start(va, f); gv::vsnprintf(buf, sizeof(buf), f, va); va_end(va);
    log_line(buf);
}

// ---------------------------------------------------------------- text drawing
namespace draw {

struct Color { float r, g, b, a; };
constexpr Color WHITE{1, 1, 1, 1}, GREY{0.6f, 0.6f, 0.65f, 1}, GOLD{1, 0.82f, 0.25f, 1}, CYAN{0.35f, 0.95f, 1, 1},
                GREEN{0.45f, 1, 0.45f, 1};

void* g_writer;
float g_scale = 0.62f;

void text(float x, float y, Color c, const char* s)
{
    gv::text_draw_raw(x + 2, y + 2, g_scale, g_scale, 0, 0, 0, 0.85f * c.a, g_writer, s);  // drop shadow
    gv::text_draw_raw(x, y, g_scale, g_scale, c.r, c.g, c.b, c.a, g_writer, s);
}

void textf(float x, float y, Color c, const char* f, ...)
{
    char buf[256];
    va_list va; va_start(va, f); gv::vsnprintf(buf, sizeof(buf), f, va); va_end(va);
    text(x, y, c, buf);
}

bool begin()
{
    g_writer = gv::text_writer();
    if (!g_writer || !gv::text_ready()) return false;

    gv::layer_begin(3);
    gv::view_2d();
    gv::text_begin();
    return true;
}

void end()
{
    gv::text_end();
    gv::layer_end();
}

// translucent backdrop: a single "■" glyph stretched over the whole panel (no quad primitive needed).
// Measured: the glyph's ink covers 26.2*scale and starts 2.2*scale (x) / 2.4*scale (y) after the draw position.
constexpr float GLYPH_INK = 26.2f, GLYPH_OX = 2.2f, GLYPH_OY = 2.4f;
void panel(float x, float y, float w, float h, float alpha)
{
    float sx = w / GLYPH_INK, sy = h / GLYPH_INK;
    gv::text_draw_raw(x - GLYPH_OX * sx, y - GLYPH_OY * sy, sx, sy, 0.02f, 0.03f, 0.08f, alpha, g_writer, "■");
}

} // namespace draw

// ---------------------------------------------------------------- Wii debug text (FUN_00fbdee0 was stubbed)
namespace wiidbg {
// The Wii-era debug console prints text cell by cell (the option menu even one character per call). We merge
// consecutive cells of the same row and colour back into strings so the proportional font lays them out properly.
struct Line { int16_t x, y, len; uint32_t rgba; char s[120]; };
constexpr int MAX_LINES = 512;
Line g_lines[MAX_LINES];
int g_count;
bool g_enabled = true;
}

extern "C" __attribute__((visibility("hidden"))) void hk_debug_print(int x, int y, uint32_t rgba, const char* s)
{
    using namespace wiidbg;
    if (!g_enabled || !s || !s[0]) return;
    size_t n = strlen(s);
    if (g_count) {
        Line& last = g_lines[g_count - 1];
        if (last.y == y && last.rgba == rgba && last.x + last.len == x && last.len + n < sizeof(last.s)) {
            memcpy(last.s + last.len, s, n + 1);
            last.len += (int16_t)n;
            return;
        }
    }
    if (g_count >= MAX_LINES) return;
    Line& l = g_lines[g_count++];
    if (n > sizeof(l.s) - 1) n = sizeof(l.s) - 1;
    l.x = (int16_t)x; l.y = (int16_t)y; l.rgba = rgba; l.len = (int16_t)n;
    memcpy(l.s, s, n);
    l.s[n] = 0;
}

static void draw_wii_debug()
{
    using namespace wiidbg;
    auto px = [](const Line& l) { return 40.0f + l.x * 12.0f; };
    auto py = [](const Line& l) { return 20.0f + l.y * 22.0f; };
    // shadows first, so a following string's shadow never covers earlier glyphs
    for (int i = 0; i < g_count; i++)
        gv::text_draw_raw(px(g_lines[i]) + 2, py(g_lines[i]) + 2, draw::g_scale, draw::g_scale, 0, 0, 0, 0.85f, draw::g_writer, g_lines[i].s);
    for (int i = 0; i < g_count; i++) {
        const Line& l = g_lines[i];
        gv::text_draw_raw(px(l), py(l), draw::g_scale, draw::g_scale, ((l.rgba >> 24) & 0xFF) / 255.f,
                          ((l.rgba >> 16) & 0xFF) / 255.f, ((l.rgba >> 8) & 0xFF) / 255.f, 1, draw::g_writer, l.s);
    }
    g_count = 0;
}

// ---------------------------------------------------------------- menu model
namespace menu {

constexpr int MAX_ITEMS = 32;
struct Item {
    char label[72];
    char value[48];
    draw::Color color;
    void (*on_a)(int arg);
    void (*on_lr)(int arg, int dir);
    int arg;
};

struct Page;
using BuildFn = void (*)(Page&);
struct Page {
    const char* title;
    BuildFn build;
    Item items[MAX_ITEMS];
    int count;
    int cursor;
    int scroll;
};

bool g_open;
bool g_ever_opened;
bool g_block_until_release;
uint64_t g_prev_buttons;
int g_hold_frames;
uint32_t g_frame;
Page* g_stack[8];
int g_depth;
char g_toast[96];
int g_toast_frames;

Item& add(Page& p, const char* label, void (*on_a)(int) = nullptr, int arg = 0)
{
    Item& it = p.items[p.count < MAX_ITEMS ? p.count++ : MAX_ITEMS - 1];
    memset(&it, 0, sizeof(it));
    size_t i = 0; for (; label[i] && i < sizeof(it.label) - 1; i++) it.label[i] = label[i];
    it.color = draw::WHITE;
    it.on_a = on_a;
    it.arg = arg;
    return it;
}

void toast(const char* s)
{
    size_t i = 0; for (; s[i] && i < sizeof(g_toast) - 1; i++) g_toast[i] = s[i];
    g_toast[i] = 0;
    g_toast_frames = 150;
}

void push(Page* p) { if (g_depth < 8) { g_stack[g_depth++] = p; p->cursor = 0; p->scroll = 0; } }

// ---------------------------------------------------------------- pages
const char* const kFlagNames[gv::NUM_FLAGS] = {
    "Daily gift", "Chests & clothes", "City Resort", "Challenges", "Snow Resort",
    "Normal events", "Mountain Resort", "Balloon crusher", "Time of day select", "Villa ceremony",
    "Challenge text", "Ending 1", "Ending 2", "Collect stamps", "(unused 14)",
    "Animal book", "Food wagon", "Air-Drop", "Ghost race",
};
const char* const kModuleNames[31] = {
    "MAIN", "MAIN_MENU", "MOVIE_PLAYER", "VILLA", "KIGAE", "BEACH_VOLLEYBALL", "SCUBA", "SKY_DIVING", "WATER_GUN",
    "TABLE_HOCKEY", "SKI_JUMP", "SNOWBALL_FIGHT", "TRAP_SHOOTING", "QUICK_SHOOTING", "VARIETY_SHOOTING",
    "RIFLE_SHOOTING", "TENNIS", "GLASS_HARP", "MINI_GOLF", "DOG_SLED", "SNOWMAN", "ICE_FISHING", "BATTING", "----",
    "PUNCHING", "MOLE", "BUCKET", "----", "PIE_THROW", "DANCE (invalid)", "DEV BOOT MENU",
};

int player_slot() { int s = gv::current_player(); return s < 0 ? 0 : s; }

void act_toggle_flag(int i)
{
    uint8_t* pd = gv::player_data(player_slot());
    if (!pd) { toast("No save data loaded"); return; }
    uint8_t& f = pd[gv::FLAGS_OFF + i];
    f = (f & 1) ? 0 : 0x07;
}

void act_unlock_resorts(int)
{
    uint8_t* pd = gv::player_data(player_slot());
    if (!pd) { toast("No save data loaded"); return; }
    static const int kResorts[] = {2, 4, 6};
    for (int i : kResorts) pd[gv::FLAGS_OFF + i] |= 0x07;
    toast("City, Snow and Mountain unlocked");
}

void act_unlock_all_flags(int)
{
    uint8_t* pd = gv::player_data(player_slot());
    if (!pd) { toast("No save data loaded"); return; }
    for (int i = 0; i < gv::NUM_FLAGS; i++) if (i != 14) pd[gv::FLAGS_OFF + i] |= 0x07;
    toast("All flags set");
}

void act_all_items(int)
{
    uint8_t* pd = gv::player_data(player_slot());
    if (!pd) { toast("No save data loaded"); return; }
    for (int i = 0; i < gv::NUM_ITEMS; i++) pd[gv::ITEMS_OFF + i] |= 1;
    toast("All 46 collectibles set");
}

void build_flags(Page& p)
{
    add(p, "Unlock resorts (City/Snow/Mountain)", act_unlock_resorts).color = draw::GOLD;
    add(p, "Set all flags", act_unlock_all_flags).color = draw::GOLD;
    uint8_t* pd = gv::player_data(player_slot());
    for (int i = 0; i < gv::NUM_FLAGS; i++) {
        if (i == 14) continue;
        Item& it = add(p, kFlagNames[i], act_toggle_flag, i);
        if (!pd) { fmt(it.value, sizeof(it.value), "-"); it.color = draw::GREY; continue; }
        uint8_t f = pd[gv::FLAGS_OFF + i];
        fmt(it.value, sizeof(it.value), (f & 1) ? "unlocked" : "locked");
        it.color = (f & 1) ? draw::GREEN : draw::WHITE;
    }
}
Page g_flags{"Progress", build_flags};

void act_open(int which);
void act_auto(int);

void act_noclip(int)      { player::toggle_noclip(); }
void act_speed(int, int d) { player::g_speed += d * 0.2f; if (player::g_speed < 0.2f) player::g_speed = 0.2f; if (player::g_speed > 5) player::g_speed = 5; }
void act_save_pos(int i)  { player::get(player::g_saved[i]); player::g_saved_valid[i] = true; toast("Position saved"); }
void act_load_pos(int i)
{
    if (!player::g_saved_valid[i]) return;
    player::set(player::g_saved[i]);
    if (player::g_noclip) player::toggle_noclip(), player::toggle_noclip();  // re-seed flight position
    toast("Teleported");
}
void act_up(int)          { float p[3]; player::get(p); p[1] += 10; player::set(p); }

void build_player(Page& p)
{
    if (!player::valid()) {
        player::ensure();
        Item& it = add(p, "Searching for player...");
        it.color = draw::GREY;
        if (scanner::g_auto_state == scanner::AUTO_FAILED) { fmt(it.value, sizeof(it.value), "not found"); add(p, "Search again", act_auto).color = draw::GOLD; }
        return;
    }
    float pos[3]; player::get(pos);
    Item& ps = add(p, "Position");
    fmt(ps.value, sizeof(ps.value), "%.1f  %.1f  %.1f", pos[0], pos[1], pos[2]);
    ps.color = draw::GREY;
    Item& nc = add(p, "Noclip / fly", act_noclip);
    fmt(nc.value, sizeof(nc.value), player::g_noclip ? "ON" : "OFF");
    nc.color = player::g_noclip ? draw::GREEN : draw::WHITE;
    Item& sp = add(p, "Fly speed  (left/right)");
    sp.on_lr = act_speed;
    fmt(sp.value, sizeof(sp.value), "< %.1f >", player::g_speed);
    Item& cf = add(p, "Player", act_auto);
    if (scanner::g_confirmed) { fmt(cf.value, sizeof(cf.value), "confirmed"); cf.color = draw::GREEN; }
    else { fmt(cf.value, sizeof(cf.value), "unsure: walk a bit"); cf.color = draw::GOLD; }
    add(p, "Move up 10 units", act_up);
    for (int i = 0; i < 3; i++) {
        char l[48];
        fmt(l, sizeof(l), "Save position %d", i + 1);
        add(p, l, act_save_pos, i);
    }
    for (int i = 0; i < 3; i++) {
        char l[48];
        fmt(l, sizeof(l), "Teleport to position %d", i + 1);
        Item& it = add(p, l, act_load_pos, i);
        if (player::g_saved_valid[i]) fmt(it.value, sizeof(it.value), "%.0f  %.0f  %.0f", player::g_saved[i][0], player::g_saved[i][1], player::g_saved[i][2]);
        else { fmt(it.value, sizeof(it.value), "-"); it.color = draw::GREY; }
    }
    add(p, "Fly: move stick, ZR up, L down, R fast").color = draw::GREY;
}
Page g_player{"Player", build_player};

void act_cam_mode(int m)  { camera::set_mode((camera::Mode)m); }
void act_cam_invert(int) { camera::g_free_invert_y = !camera::g_free_invert_y; }
void act_cam_speed(int, int d) { camera::g_fov_speed += d * 0.1f; if (camera::g_fov_speed < 0.1f) camera::g_fov_speed = 0.1f; if (camera::g_fov_speed > 3) camera::g_fov_speed = 3; }

void build_camera(Page& p)
{
    static const char* const names[] = {"Game camera (normal)", "Orbit camera (right stick)", "Free camera (freecam)"};
    for (int m = 0; m < 3; m++) {
        Item& it = add(p, names[m], act_cam_mode, m);
        if (camera::g_mode == m) { fmt(it.value, sizeof(it.value), "active"); it.color = draw::GREEN; }
    }
    Item& inv = add(p, "Freecam: invert Y axis", act_cam_invert);
    fmt(inv.value, sizeof(inv.value), camera::g_free_invert_y ? "ON" : "OFF");
    Item& sp = add(p, "Freecam speed  (left/right)");
    sp.on_lr = act_cam_speed;
    fmt(sp.value, sizeof(sp.value), "< %.1f >", camera::g_fov_speed);
    add(p, "Orbit: right stick = turn and look up/down").color = draw::GREY;
    add(p, "            D-pad up/down = closer/farther").color = draw::GREY;
    add(p, "Freecam: left stick flies, right stick looks,").color = draw::GREY;
    add(p, "            ZR up, L down, R fast").color = draw::GREY;
}
Page g_camera{"Camera", build_camera};

void act_toggle_wiidbg(int) { wiidbg::g_enabled = !wiidbg::g_enabled; }

void act_devmenu(int)
{
    if (devmenu::g_active) { devmenu::close(); return; }
    if (devmenu::open()) { g_open = false; g_block_until_release = true; toast("Developer editor open - use the game controls"); }
    else toast("Developer editor could not be created");
}

void act_scan_start(int)   { scanner::start(); toast(scanner::g_overflow ? "Scan full - continue anyway" : "Scan done - now close the menu and walk"); }
void act_scan_moved(int)   { scanner::filter(true); }
void act_scan_still(int)   { scanner::filter(false); }
void act_scan_undo(int)    { scanner::undo(); }
void act_half_test(int first) { scanner::lift_half(first, 5.0f); toast("Moved - did the player jump sideways?"); }
void act_half_keep(int first) { scanner::keep_half(first); }
void act_cand_lift(int i)  { if (i < scanner::g_count) ((float*)scanner::g_cands[i].addr)[0] += 5.0f; }

void act_auto(int) { scanner::auto_start(); }

void build_scan(Page& p)
{
    Item& au = add(p, "Find player", act_auto);
    au.color = draw::GOLD;
    switch (scanner::g_auto_state) {
    case scanner::AUTO_COLLECT: fmt(au.value, sizeof(au.value), "searching..."); break;
    case scanner::AUTO_RUNNING: fmt(au.value, sizeof(au.value), "testing %d / %d", scanner::g_auto_index, scanner::g_auto_count); break;
    case scanner::AUTO_PTRSCAN: fmt(au.value, sizeof(au.value), "found, analysing..."); au.color = draw::GREEN; break;
    case scanner::AUTO_FOUND:   fmt(au.value, sizeof(au.value), "found: %p", (void*)scanner::g_auto_found); au.color = draw::GREEN; break;
    case scanner::AUTO_FAILED:  fmt(au.value, sizeof(au.value), "not found"); break;
    default: break;
    }
    add(p, "1. New scan  (stand still first)", act_scan_start).color = draw::GOLD;
    add(p, "2. I walked  ->  filter: changed", act_scan_moved);
    add(p, "3. I stand still  ->  filter: unchanged", act_scan_still);
    Item& u = add(p, "Undo last filter", act_scan_undo);
    if (scanner::g_undo_count < 0) u.color = draw::GREY;
    Item& c = add(p, "Candidates");
    fmt(c.value, sizeof(c.value), "%d%s  (round %d)", scanner::g_count, scanner::g_overflow ? "+" : "", scanner::g_round);
    c.color = draw::GREY;
    if (scanner::g_count > 1) {
        add(p, "Bisect: move half A (x+5)", act_half_test, 1).color = draw::CYAN;
        add(p, "Bisect: move half B (x+5)", act_half_test, 0).color = draw::CYAN;
        add(p, "   Player moved with A  ->  keep A", act_half_keep, 1);
        add(p, "   Player moved with B  ->  keep B", act_half_keep, 0);
    }
    if (scanner::g_count > 0 && scanner::g_count <= 12)
        for (int i = 0; i < scanner::g_count; i++) {
            const float* f = (const float*)scanner::g_cands[i].addr;
            char label[64];
            fmt(label, sizeof(label), "%p  (A: x+5)", (void*)scanner::g_cands[i].addr);
            Item& it = add(p, label, act_cand_lift, i);
            fmt(it.value, sizeof(it.value), "%.1f  %.1f  %.1f", f[0], f[1], f[2]);
            it.color = draw::GREEN;
        }
}
Page g_scan{"Position search", build_scan};

void build_info(Page& p)
{
    int mod = gv::current_module();
    fmt(add(p, "Module").value, 48, "%d %s", mod, (mod >= 0 && mod < 31) ? kModuleNames[mod] : "?");
    fmt(add(p, "Player slot").value, 48, "%d", gv::current_player());
    fmt(add(p, "main base").value, 48, "%p", (void*)__main_start);
    fmt(add(p, "Plugin base").value, 48, "%p", (void*)__module_start);
    fmt(add(p, "Frame").value, 48, "%u", g_frame);
    for (int i = 0; i < p.count; i++) p.items[i].color = draw::GREY;
}
Page g_info{"Info", build_info};

void act_radio_song(int, int d) { radio::g_song = (radio::g_song + d + radio::SONG_COUNT) % radio::SONG_COUNT; }
void act_radio_play(int) { radio::play_now(); toast("Loudspeakers will switch songs shortly"); }
void act_radio_toggle(int) { radio::g_enabled = !radio::g_enabled; }

void build_radio(Page& p)
{
    Item& s = add(p, "Song  (left/right)", act_radio_play);
    s.on_lr = act_radio_song;
    fmt(s.value, sizeof(s.value), "< %s >", radio::SONGS[radio::g_song]);
    add(p, "Play now  (A)", act_radio_play).color = draw::GOLD;
    Item& on = add(p, "Radio control", act_radio_toggle);
    fmt(on.value, sizeof(on.value), radio::g_enabled ? "ON (song repeats)" : "OFF (normal playlist)");
    on.color = radio::g_enabled ? draw::GREEN : draw::WHITE;
    Item& now = add(p, "Now playing");
    fmt(now.value, sizeof(now.value), "%s", radio::g_playing >= 0 ? radio::SONGS[radio::g_playing] : "-");
    now.color = draw::GREY;
    add(p, "Applies to the loudspeakers in the resorts.").color = draw::GREY;
    add(p, "The switch happens with the next sound in the game.").color = draw::GREY;
}
Page g_radio{"Radio", build_radio};

void build_main(Page& p)
{
    add(p, "Player: teleport & noclip  >", act_open, 4);
    add(p, "Camera: orbit & freecam  >", act_open, 5);
    add(p, "Radio: loudspeaker music  >", act_open, 6);
    add(p, "Progress & resorts  >", act_open, 1);
    uint8_t* pd = gv::player_data(player_slot());
    int owned = 0;
    if (pd) for (int i = 0; i < gv::NUM_ITEMS; i++) owned += pd[gv::ITEMS_OFF + i] & 1;
    Item& items = add(p, "Collectibles: own all", act_all_items);
    fmt(items.value, sizeof(items.value), pd ? "%d / 46" : "-", owned);
    Item& dm = add(p, "Original developer editor (experimental)", act_devmenu);
    fmt(dm.value, sizeof(dm.value), devmenu::g_active ? "OPEN" : "");
    dm.color = draw::GOLD;
    add(p, "Position search (tool)  >", act_open, 3);
    Item& w = add(p, "Wii debug text (old debug modes only)", act_toggle_wiidbg);
    fmt(w.value, sizeof(w.value), wiidbg::g_enabled ? "ON" : "OFF");
    add(p, "Info  >", act_open, 2);
}
Page g_main{"GO VACATION  Mod Menu", build_main};

void act_open(int which)
{
    Page* pages[] = {&g_info, &g_flags, &g_info, &g_scan, &g_player, &g_camera, &g_radio};
    push(pages[which]);
}

// ---------------------------------------------------------------- input + drawing
constexpr float X = 80, Y = 60, LINE = 30, VALUE_X = 640;
constexpr int VISIBLE = 16;

void update(uint64_t held)
{
    uint64_t pressed = held & ~g_prev_buttons;
    g_prev_buttons = held;

    // auto-repeat for d-pad
    if (held & (BTN_UP | BTN_DOWN)) g_hold_frames++; else g_hold_frames = 0;
    if (g_hold_frames > 18 && (g_hold_frames % 4) == 0) pressed |= held & (BTN_UP | BTN_DOWN);

    bool combo = (held & BTN_ZL) && (pressed & BTN_MINUS);
    if (!g_open) {
        if (g_block_until_release && !held) g_block_until_release = false;
        if (combo) { g_open = true; g_ever_opened = true; if (g_depth == 0) push(&g_main); }
        return;
    }
    if (combo) { g_open = false; g_block_until_release = true; return; }

    Page& p = *g_stack[g_depth - 1];
    if (pressed & BTN_B) {
        if (--g_depth == 0) { g_open = false; g_block_until_release = true; }
        return;
    }
    if (p.count == 0) return;
    if (pressed & BTN_UP)   p.cursor = (p.cursor + p.count - 1) % p.count;
    if (pressed & BTN_DOWN) p.cursor = (p.cursor + 1) % p.count;
    Item& it = p.items[p.cursor];
    if ((pressed & BTN_A) && it.on_a) it.on_a(it.arg);
    if ((pressed & (BTN_LEFT | BTN_RIGHT)) && it.on_lr) it.on_lr(it.arg, (pressed & BTN_LEFT) ? -1 : 1);
}

void render()
{
    Page& p = *g_stack[g_depth - 1];
    p.count = 0;
    p.build(p);
    if (p.cursor >= p.count) p.cursor = p.count ? p.count - 1 : 0;
    if (p.cursor < p.scroll) p.scroll = p.cursor;
    if (p.cursor >= p.scroll + VISIBLE) p.scroll = p.cursor - VISIBLE + 1;

    // breadcrumb title
    char title[128] = {0};
    size_t n = 0;
    for (int d = 0; d < g_depth; d++) {
        const char* t = g_stack[d]->title;
        if (d) { const char* sep = "  /  "; while (*sep && n < sizeof(title) - 1) title[n++] = *sep++; }
        while (*t && n < sizeof(title) - 1) title[n++] = *t++;
    }
    int shown = p.count - p.scroll < VISIBLE ? p.count - p.scroll : VISIBLE;
    draw::panel(X - 30, Y - 22, 1060, LINE * (shown + 2.7f) + 34, 0.6f);
    draw::text(X, Y, draw::GOLD, title);
    draw::text(X, Y + LINE * 0.9f, draw::GREY, "--------------------------------------------------------------");

    float y = Y + LINE * 2;
    for (int i = p.scroll; i < p.count && i < p.scroll + VISIBLE; i++, y += LINE) {
        Item& it = p.items[i];
        bool sel = i == p.cursor;
        draw::Color c = sel ? draw::CYAN : it.color;
        draw::textf(X, y, c, "%s %s", sel ? ">" : " ", it.label);
        if (it.value[0]) draw::text(VALUE_X, y, sel ? draw::CYAN : it.color, it.value);
    }
    if (p.scroll > 0) draw::text(VALUE_X + 220, Y + LINE * 2, draw::GREY, "^");
    if (p.scroll + VISIBLE < p.count) draw::text(VALUE_X + 220, y - LINE, draw::GREY, "v");

    // the renderer clips each string at ~430 px (at our scale), so the footer is drawn in parts
    float fy = y + LINE * 0.5f;
    draw::text(X, fy, draw::GREY, "A: Select    B: Back");
    draw::text(X + 330, fy, draw::GREY, "D-pad: Navigate");
    draw::text(X + 640, fy, draw::GREY, "ZL + Minus: Close");
}

} // namespace menu

bool menu_wants_input() { return menu::g_open || menu::g_block_until_release; }

// ---------------------------------------------------------------- autoplay (test builds only)
#ifdef GV_AUTOPLAY
namespace autoplay {
int g_ingame_frames, g_phase;
uint32_t g_t0;
void tick(uint32_t f)
{
    g_inject = {};
    const float* ref = (const float*)(__main_start + 0x7CB5800);
    bool in_game = gv::player_data(0) && (ref[0] > 1 || ref[0] < -1) && gv::current_module() == 0;
    g_ingame_frames = in_game ? g_ingame_frames + 1 : 0;
    if (f % 120 == 0) logf("[gvmod] autoplay f=%u module=%d ingame=%d phase=%d ref=%.1f", f, gv::current_module(), g_ingame_frames, g_phase, ref[0]);
    if (g_phase == 0) {
        if (g_ingame_frames > 180) { g_phase = 1; g_t0 = f; log_line("[gvmod] autoplay: in game"); return; }
        if (f < 700) return;
        uint32_t k = f % 240;
        if (k < 8) g_inject.buttons = BTN_L | BTN_R;
        else if (k >= 120 && k < 128) g_inject.buttons = BTN_A;
        return;
    }
    uint32_t t = f - g_t0;
#ifdef GV_FAULTTEST
    if (t == 60) {
        // make the page holding the camera eye copy read-only: the next writer faults and Ryujinx logs its PC
        uint64_t page = ((uint64_t)__main_start + 0x59B2F90) & ~0xFFFull;
        register uint64_t x0 __asm__("x0") = page;
        register uint64_t x1 __asm__("x1") = 0x1000;
        register uint64_t x2 __asm__("x2") = 1;  // R
        __asm__ volatile("svc 0x2" : "+r"(x0), "+r"(x1), "+r"(x2) :: "memory");  // svcSetMemoryPermission
        logf("[gvmod] faulttest: SetMemoryPermission(%p) = %x", (void*)page, (unsigned)x0);
        if (x0 != 0) {
            register uint64_t y0 __asm__("x0") = 0xFFFF8001;
            register uint64_t y1 __asm__("x1") = page;
            register uint64_t y2 __asm__("x2") = 0x1000;
            register uint64_t y3 __asm__("x3") = 1;
            __asm__ volatile("svc 0x73" : "+r"(y0), "+r"(y1), "+r"(y2), "+r"(y3) :: "memory");  // svcSetProcessMemoryPermission
            logf("[gvmod] faulttest: SetProcessMemoryPermission = %x", (unsigned)y0);
        }
    }
    return;
#endif
    // 1) click away the welcome dialogs
    if (t < 1200) { if (t % 120 < 6) g_inject.buttons = BTN_A; return; }
    // 2) find the player
    if (t == 1300) { scanner::auto_start(); log_line("[gvmod] autoplay: auto search"); }
    if (t > 1300 && t % 60 == 0) logf("[gvmod] autoplay: search state %d", (int)scanner::g_auto_state);
    static uint32_t t_found;
    if (!t_found) { if (scanner::g_auto_state == scanner::AUTO_FOUND || scanner::g_auto_state == scanner::AUTO_FAILED) t_found = t; return; }
    uint32_t u = t - t_found;
    // 3) orbit: rotate, look up, look down; then free camera
    if (u == 60)  { camera::set_mode(camera::CAM_ORBIT); log_line("[gvmod] autoplay: orbit on"); }
    if (u >= 120 && u < 240) g_inject.rx = 24000;
    if (u >= 300 && u < 360) g_inject.ry = 24000;
    if (u >= 420 && u < 540) g_inject.ry = -24000;
    if (u == 600) { camera::set_mode(camera::CAM_FREE); log_line("[gvmod] autoplay: free on"); }
    if (u >= 660 && u < 780) g_inject.ly = 24000;
    if (u == 900) { camera::set_mode(camera::CAM_ORBIT); player::toggle_noclip(); logf("[gvmod] autoplay: noclip on (%d)", (int)player::g_noclip); }
    if (u >= 960 && u < 1080) { g_inject.ly = 24000; g_inject.buttons = BTN_ZR; }
    if (u == 1140) { player::toggle_noclip(); camera::set_mode(camera::CAM_GAME); log_line("[gvmod] autoplay: done"); }
}
}
#endif

// ---------------------------------------------------------------- frame hook
extern "C" __attribute__((visibility("hidden"))) uint64_t hk_end_frame(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    using namespace menu;
    g_frame++;
    if (g_frame == 1) logf("[gvmod] first frame, main=%p plugin=%p", __main_start, __module_start);

#ifdef GV_AUTOPLAY
    autoplay::tick(g_frame);
#endif
#ifndef GV_DEVMENU   // the developer menu build only shows the game's own debug text, see build.py
    camera::diag_frame(g_frame);
    Input in = read_input();
    menu::update(in.buttons);
    scanner::auto_tick();
    player::tick(in.buttons, in.lx, in.ly, g_open);
    camera::tick(in, g_open);
#ifdef GV_AUTOTEST
    if (g_frame == 1200 && !g_open) { g_open = true; if (g_depth == 0) push(&g_main); }
#endif

    devmenu::tick();
#endif
    // The developer boot menu (module 30) runs before anything creates the text writer: create it ourselves
    // with the game's own initialiser FUN_000071cc (allocates the writer and loads gvn-Seurat-DB.bfttf).
    static bool s_writer_init;
    if (!s_writer_init && !gv::text_writer() && gv::current_module() == 30) {
        s_writer_init = true;
        game<void (*)()>(0x71CC)();
        logf("[gvmod] created text writer for the developer boot menu: %p", gv::text_writer());
    }
    static int s_dbg_total;
    s_dbg_total += wiidbg::g_count;
    if (g_frame % 300 == 0) {
        void** mm = *(void***)(__main_start + 0x568A390);
        logf("[gvmod] diag: writer=%p msgmgr=%p debugprints=%d module=%d", gv::text_writer(), mm ? *mm : nullptr, s_dbg_total, gv::current_module());
        s_dbg_total = 0;
    }
    // Touch the game's 2D layer only when there is something to show. Drawing every frame crashed while an
    // activity was loading: its layer object exists then but cannot be used yet. After a module change (an
    // activity or resort starts) wait a second before drawing again.
    // A new module frees the old player object and camera, so drop them.
    static int s_module = -1, s_settle;
    int module = gv::current_module();
    if (module != s_module) {
#ifndef GV_DEVMENU
        if (s_module >= 0) { player::forget(); camera::set_mode(camera::CAM_GAME); }
#endif
        s_module = module; s_settle = 60;
    }
    if (s_settle > 0) s_settle--;
    // Many activities run on their own engine: they never use the main renderer's camera and have none of the
    // 2D layers we draw into. Only draw (and only allow the menu) while the main renderer is active, or in the
    // developer boot menu.
    static unsigned s_views;
    static int s_no_view = 1000;
    if (camera::g_view_count != s_views) { s_views = camera::g_view_count; s_no_view = 0; }
    else if (s_no_view < 1000) s_no_view++;
    bool can_draw = s_settle == 0 && (s_no_view < 10 || module == 30);
    if (!can_draw && g_open) g_open = false;
#ifdef GV_DEVMENU
    bool hint = false;
#else
    bool hint = !g_ever_opened && g_frame < 1800;
#endif
    bool want = g_open || hint || g_toast_frames > 0 || wiidbg::g_count > 0;
    if (want && can_draw && draw::begin()) {
        draw_wii_debug();
        if (g_open) render();
        else if (hint) draw::text(24, 16, draw::GOLD, "GO VACATION Mod  -  ZL + Minus: Menu");
        if (g_toast_frames > 0) { g_toast_frames--; draw::text(80, 650, draw::GREEN, g_toast); }
        draw::end();
    } else {
        wiidbg::g_count = 0;
        if (g_toast_frames > 0) g_toast_frames--;
    }
    return game<uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t)>(gv::END_FRAME)(a0, a1, a2, a3);
}

// rtld calls DT_INIT after relocating us (sdk is not initialised yet, so nothing fancy here).
extern "C" __attribute__((visibility("hidden"))) void gv_init()
{
    // sanity check: the debug print function must start with the branch to hook slot 0 that the main patch writes
    constexpr uint32_t hook_branch = 0x14000000 | (((gv::hook_entry(0) - gv::DEBUG_PRINT) >> 2) & 0x3FFFFFF);
    if (game_var<uint32_t>(gv::DEBUG_PRINT) != hook_branch) log_line("[gvmod] WARNING: main not where expected");
    else log_line("[gvmod] loaded");
}
