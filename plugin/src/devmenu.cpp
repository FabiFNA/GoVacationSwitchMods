// The game's own (disabled) developer save-data editor: System::Menu::CMenuSelect, menu id 0x16.
// Nothing in the release build creates it; we build it the same way other menus are built (e.g. FUN_00caedcc):
// a small "menu launcher" object whose vtable+0x20 is the menu factory (FUN_00ca8088), then Start (job vtable+0x10)
// and one Update (job vtable+0x18) per frame. Its pages draw themselves with the game's text renderer and read the
// game's own pad state.
#include "plugin.h"

namespace devmenu {

constexpr uintptr_t LAUNCHER_VTABLE_GOT = 0x56AA2B8;  // -> vtable base of the launcher class (0x54330D8)
constexpr uintptr_t LAUNCHER_CTOR = 0xBBD7E8;
constexpr uintptr_t LAUNCHER_START = 0xBBD84C;
constexpr int MENU_SELECT_ID = 0x16;

uint64_t g_launcher[3];
bool g_active;
int g_status;

static void** job() { return (void**)g_launcher[1]; }
template <typename F> static F vfunc(void* obj, int off) { return *(F*)(*(char**)obj + off); }

bool open()
{
    if (g_active) return true;
    for (auto& v : g_launcher) v = 0;
    game<void (*)(uint64_t*)>(LAUNCHER_CTOR)(g_launcher);
    char* vtbase = *(char**)(__main_start + LAUNCHER_VTABLE_GOT);
    g_launcher[0] = (uint64_t)(vtbase + 0x10);
    auto factory = *(int (**)(uint64_t*, int, int))(vtbase + 0x20);
    int r = factory(g_launcher, MENU_SELECT_ID, 1);
    logf("[gvmod] devmenu: factory -> %d, job %p", r, job());
    if (!job()) return false;
    game<int (*)(uint64_t*)>(LAUNCHER_START)(g_launcher);
    g_active = true;
    g_status = 0;
    return true;
}

void close()
{
    if (!g_active) return;
    g_active = false;
    if (job()) vfunc<void (*)(void*)>(job(), 0x20)(job());  // End: frees its BootData buffers
    logf("[gvmod] devmenu: closed (status %d)", g_status);
}

// once per frame from the end-of-frame hook, inside the game's render frame
void tick()
{
    if (!g_active || !job()) return;
    g_status = vfunc<int (*)(void*, uint64_t)>(job(), 0x18)(job(), 0);
}

} // namespace devmenu
