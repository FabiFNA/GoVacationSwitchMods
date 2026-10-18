// Controller input.
// We export the nn::hid::GetNpadState overloads that main imports. rtld binds main's imports to the first module
// defining them, and we are loaded before sdk, so every pad read of the game goes through us. We forward to the
// real sdk implementation (looked up in sdk's symbol table) and can hide buttons while the menu is open.
#include "plugin.h"

Input g_inject;  // synthetic input (autoplay tests)

namespace {

struct Elf64Sym { uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size; };

using GetNpadStateFn = void (*)(NpadState*, const uint32_t&);

enum Style { FULLKEY, HANDHELD, JOYDUAL, JOYLEFT, JOYRIGHT, STYLE_COUNT };
const char* const kSymbols[STYLE_COUNT] = {
    "_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj",
    "_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj",
    "_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj",
    "_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj",
    "_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj",
};
GetNpadStateFn g_real[STYLE_COUNT];
bool g_resolved;

bool streq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

// sdk is mapped directly after us (page aligned).
void resolve()
{
    if (g_resolved) return;
    g_resolved = true;
    uintptr_t base = ((uintptr_t)__module_end + 0xFFF) & ~(uintptr_t)0xFFF;
    uint32_t mod0 = *(uint32_t*)(base + 4);
    if (*(uint32_t*)(base + mod0) != 0x30444F4D) { log_line("sdk MOD0 not found"); return; }  // "MOD0"
    auto* dyn = (int64_t*)(base + mod0 + *(int32_t*)(base + mod0 + 4));
    uintptr_t hash = 0, strtab = 0, symtab = 0;
    for (; dyn[0] != 0; dyn += 2) {
        uintptr_t v = (uintptr_t)dyn[1];
        if (v < 0x10000000) v += base;  // unrelocated d_ptr
        if (dyn[0] == 4) hash = v; else if (dyn[0] == 5) strtab = v; else if (dyn[0] == 6) symtab = v;
    }
    if (!hash || !strtab || !symtab) { log_line("sdk dynamic incomplete"); return; }
    uint32_t nchain = ((uint32_t*)hash)[1];
    auto* syms = (Elf64Sym*)symtab;
    for (uint32_t i = 0; i < nchain; i++) {
        if (!syms[i].shndx) continue;
        const char* n = (const char*)(strtab + syms[i].name);
        for (int s = 0; s < STYLE_COUNT; s++)
            if (!g_real[s] && streq(n, kSymbols[s])) g_real[s] = (GetNpadStateFn)(base + syms[i].value);
    }
    log_line(g_real[FULLKEY] ? "hid resolved" : "hid NOT resolved");
}

void call(Style s, NpadState* out, const uint32_t& id)
{
    resolve();
    if (g_real[s]) g_real[s](out, id);
    else { for (size_t i = 0; i < sizeof(*out); i++) ((char*)out)[i] = 0; }
    if (g_inject.buttons || g_inject.rx || g_inject.ry || g_inject.lx || g_inject.ly) {
        out->buttons |= g_inject.buttons;
        if (g_inject.lx || g_inject.ly) { out->lx = g_inject.lx; out->ly = g_inject.ly; }
        if (g_inject.rx || g_inject.ry) { out->rx = g_inject.rx; out->ry = g_inject.ry; }
        out->attributes |= 1;
    }
    if (menu_wants_input()) { out->buttons = 0; out->lx = out->ly = out->rx = out->ry = 0; }
    if (out->buttons & BTN_ZL) out->buttons &= ~(uint64_t)BTN_MINUS;  // ZL+Minus belongs to the mod menu
    if (player::g_noclip || camera::g_mode == camera::CAM_FREE) {  // flight controls belong to us
        out->buttons &= ~(uint64_t)(BTN_ZR | BTN_L | BTN_R);
        out->lx = out->ly = 0;
    }
    if (camera::g_mode != camera::CAM_GAME) {  // right stick + d-pad zoom drive our camera
        out->rx = out->ry = 0;
        out->buttons &= ~(uint64_t)(BTN_UP | BTN_DOWN);
    }
}

} // namespace

Input read_input()
{
    resolve();
    Input in{};
    NpadState st;
    const uint32_t ids[] = { 0, 0x20 };  // player 1, handheld
    for (uint32_t id : ids)
        for (int s = 0; s < STYLE_COUNT; s++)
            if (g_real[s]) {
                st = {};
                g_real[s](&st, id);
                if (!(st.attributes & 1)) continue;
                in.buttons |= st.buttons;
                if (st.lx || st.ly) { in.lx = st.lx; in.ly = st.ly; }
                if (st.rx || st.ry) { in.rx = st.rx; in.ry = st.ry; }
            }
    in.buttons |= g_inject.buttons;
    if (g_inject.rx || g_inject.ry) { in.rx = g_inject.rx; in.ry = g_inject.ry; }
    if (g_inject.lx || g_inject.ly) { in.lx = g_inject.lx; in.ly = g_inject.ly; }
    return in;
}

#define EXPORT extern "C" __attribute__((visibility("default")))
EXPORT void hid_fullkey(NpadState* o, const uint32_t& id)  __asm__("_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj");
EXPORT void hid_handheld(NpadState* o, const uint32_t& id) __asm__("_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj");
EXPORT void hid_joydual(NpadState* o, const uint32_t& id)  __asm__("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj");
EXPORT void hid_joyleft(NpadState* o, const uint32_t& id)  __asm__("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj");
EXPORT void hid_joyright(NpadState* o, const uint32_t& id) __asm__("_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj");

void hid_fullkey(NpadState* o, const uint32_t& id)  { call(FULLKEY, o, id); }
void hid_handheld(NpadState* o, const uint32_t& id) { call(HANDHELD, o, id); }
void hid_joydual(NpadState* o, const uint32_t& id)  { call(JOYDUAL, o, id); }
void hid_joyleft(NpadState* o, const uint32_t& id)  { call(JOYLEFT, o, id); }
void hid_joyright(NpadState* o, const uint32_t& id) { call(JOYRIGHT, o, id); }
