// Radio: choose the song the resort loudspeakers play.
// Every sound of the game goes through nn::atk::SoundArchivePlayer::detail_SetupSoundImpl (0x8F3DB8), called with
// the sound's ItemId. The loudspeaker songs are stream sounds 2076..2195 (front channels, several playlists that
// reuse the same 20 songs) and 2196..2315 (the matching rear channel, always +120). While the radio is on we swap
// every loudspeaker song for the chosen one. "Play now" stops the running loudspeaker song so the game's own
// playlist logic starts the next one, which we then swap as well.
#include "plugin.h"

namespace radio {

constexpr uint32_t FRONT_FIRST = 2076, FRONT_LAST = 2195, REAR_OFFSET = 120;
constexpr uintptr_t BASIC_SOUND_STOP = 0x8F74E4;  // nn::atk::detail::BasicSound::Stop(int fadeFrames)
constexpr int FADE_FRAMES = 30;

const char* const SONGS[SONG_COUNT] = {
    "banbini", "bgmdelivery", "blocktown", "coffeebreak", "eventbgm", "gamestart rallyX", "highscore digdug",
    "ingame digdug", "ingame mappy", "ingame rallyX", "moji futari", "nameentry galaga", "piecevole",
    "startmusic pacman", "toypop", "toypop nameentry", "wordswords", "yourvibe", "ridgeracer", "urbanrain",
};

// song of each front-channel sound 2076..2195 (read from the sound archive)
static const uint8_t SONG_OF[FRONT_LAST - FRONT_FIRST + 1] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,0,1,2,3,4,5,9,10,11,12,3,4,6,11,15,12,18,7,8,0,10,16,3,4,6,
    11,15,12,18,7,8,0,10,16,3,4,6,11,15,12,13,14,13,13,13,13,13,13,13,13,13,13,13,13,13,14,14,14,14,14,14,14,14,
    14,14,14,14,14,14,14,3,5,11,15,10,17,18,9,8,7,2,4,0,10,16,3,5,11,15,10,17,18,9,8,7,2,4,0,10,16,
};

bool g_enabled;
int g_song;
int g_playing = -1;          // song currently coming out of the loudspeakers (-1 = unknown)
volatile bool g_stop_pending;

// Loudspeaker sounds we started. The SoundHandle the game passes in can be a short-lived temporary, so we keep the
// sound object itself plus its start tick (BasicSound+0x110, set by the setup function). Sound objects come from a
// fixed pool and get reused, so the tick tells us whether the object still plays our song.
constexpr int MAX_SOUNDS = 8;
struct Playing { uint8_t* sound; uint64_t tick; };
Playing g_sounds[MAX_SOUNDS];
int g_sound_next;

void remember(uint8_t* sound)
{
    if (!sound) return;
    g_sounds[g_sound_next] = {sound, *(uint64_t*)(sound + 0x110)};
    g_sound_next = (g_sound_next + 1) % MAX_SOUNDS;
}

// the front-channel sound closest to `front` that plays `song` (keeps the playlist's own 3D and volume setup)
static uint32_t front_for(uint32_t front, int song)
{
    uint32_t best = 0, best_d = ~0u;
    for (uint32_t i = FRONT_FIRST; i <= FRONT_LAST; i++) {
        if (SONG_OF[i - FRONT_FIRST] != song) continue;
        uint32_t d = i > front ? i - front : front - i;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

static void stop_all()
{
    for (auto& p : g_sounds) {
        if (!p.sound) continue;
        if (*(void**)p.sound && *(uint64_t*)(p.sound + 0x110) == p.tick) {
            logf("[gvmod] radio: stopping sound %p", (void*)p.sound);
            game<void (*)(void*, int)>(BASIC_SOUND_STOP)(p.sound, FADE_FRAMES);
        }
        p.sound = nullptr;
    }
}

bool is_speaker(uint32_t id)
{
    if ((id >> 24) != 1) return false;
    uint32_t idx = id & 0xFFFFFF;
    return idx >= FRONT_FIRST && idx <= FRONT_LAST + REAR_OFFSET;
}

// runs on the game's sound thread, so the stop request is carried out here as well
uint32_t filter(uint32_t id)
{
    if (g_stop_pending) { g_stop_pending = false; stop_all(); }
    if ((id >> 24) != 1) return id;
    uint32_t idx = id & 0xFFFFFF;
    bool rear = idx >= FRONT_FIRST + REAR_OFFSET && idx <= FRONT_LAST + REAR_OFFSET;
    uint32_t front = rear ? idx - REAR_OFFSET : idx;
    if (front < FRONT_FIRST || front > FRONT_LAST) return id;
    uint32_t out = front;
    if (g_enabled) out = front_for(front, g_song);
    g_playing = SONG_OF[out - FRONT_FIRST];
    uint32_t res = (id & 0xFF000000) | (rear ? out + REAR_OFFSET : out);
    logf("[gvmod] radio: %s %u (%s) -> %u (%s)", rear ? "rear" : "front", idx, SONGS[SONG_OF[front - FRONT_FIRST]],
         res & 0xFFFFFF, SONGS[g_playing]);
    return res;
}

void play_now()
{
    g_enabled = true;
    g_stop_pending = true;
}

} // namespace radio

// nn::atk::SoundArchivePlayer::detail_SetupSoundImpl(this, handle, soundId, ambientArgInfo, actor, holdFlag, startInfo, ...)
extern "C" __attribute__((visibility("hidden")))
int hk_setup_sound(void* self, void** handle, uint32_t id, void* amb, void* actor, uint32_t hold, void* info, void* x7)
{
    id = radio::filter(id);
    int r = game<int (*)(void*, void**, uint32_t, void*, void*, uint32_t, void*, void*)>(gv::SETUP_SOUND)(
        self, handle, id, amb, actor, hold, info, x7);
    if (r == 0 && handle && radio::is_speaker(id)) radio::remember((uint8_t*)*handle);
    return r;
}
