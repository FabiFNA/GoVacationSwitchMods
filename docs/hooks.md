# Hooks

## The hooks

HOOKS in plugin/build.py is the list of all hooks. Each entry has a code cave slot and the hooked function and its call sites. The slot numbers must match hook_entry in plugin/src/game.h.

| Slot | Hook | Function | Call sites | Used by |
| --- | --- | --- | --- | --- |
| 0 | hk_debug_print | 0xFBDEE0 debug print | patched at its start | both mods |
| 1 | hk_end_frame | 0x534D3C end of frame | 6 | both mods |
| 2 | hk_get_view | 0x04A858 get view matrix | 7 | both mods |
| 3 | hk_set_view | 0x04A7F0 set view matrix | 2 | GV ModMenu |
| 4 | hk_setup_sound | 0x8F3DB8 sound start of nn::atk | 4 | GV ModMenu |

The debug print function is special. On Switch it only returns. Nobody needs the original. So the patch puts the branch at its start instead of changing its callers.


## Why calls are redirected

A hook at the start of a function would overwrite the first instruction of the game. The hook would then have to run that instruction itself. That means copying code of the game into the patch. Redirecting the calls avoids that. The functions stay untouched and the patch only holds branches we compute.

This only works when every caller uses a direct bl. A function that is also called through a pointer would be missed for those calls.


## Adding a hook

1. Find the function in main. Note its address relative to the start of main.
2. Find every call site. Scan the code segment of main for bl instructions that target the function. Also look for b instructions. A tail call with b can not be redirected this way.
3. Make sure no pointer to the function exists. Search the data and the relocations of main for its address. If there is one this approach does not catch all calls.
4. Add the function address to game.h. Write the hook in the plugin with the same arguments. Call the original with game and the address from game.h.
5. Add an entry to HOOKS in build.py with the next free slot. Add the hook name to the mods that need it in MODS.
6. Build with the exefs folder in place. The build then checks every call site. It stops with a message when a call site is not a bl to the function.
7. Test in Ryujinx.

A hook must return the same value as the original. It must keep every argument as it is unless changing it is the point of the hook.


## Patches that are not hooks

| Address | What the patch does | Mod |
| --- | --- | --- |
| 0xCFB754 | ret at the entry of the AVATAR TEST handler | both |
| 0xCAAAC8 and 0xCAABC4 | cursor 0 instead of minus 1 in the developer editor | both |
| 0x00039C | boot module 30 instead of module 1 | GV DevMenu |
| 0x56CD58 | accept module numbers up to 30 | GV DevMenu |

The developer editor has a page for activities. When its list is missing it sets its cursor to minus 1. Pressing left or right then reads outside an array and crashes. The fix sets the cursor to 0.
