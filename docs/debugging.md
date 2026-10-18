# Debugging

## Logs

The plugin writes log lines with the debug output of the system. Ryujinx puts them into its log file. Every line of the plugin starts with [gvmod].

Ryujinx keeps its logs in the Logs folder next to Ryujinx.exe. The newest file is the current session. Guest must be one of the enabled log levels in the logging settings of Ryujinx. The first lines of each log file list the enabled levels.

Useful lines:

| Line | Meaning |
| --- | --- |
| loaded | gv_init ran and the patch is in place |
| WARNING: main not where expected | The patch is missing or main is another version |
| first frame | The frame hook runs |
| diag | Every 300 frames: text writer and module number |
| auto | The player search and what it found |
| verify | The check of the picked player object while walking |
| getview and setview | How often the camera functions were called |


## Crashes

When the game crashes Ryujinx writes a guest stack trace and the registers into the log. Look for Guest stack trace.

1. Lines with gvn.nss are inside main. The number after it is the offset from the start of main. It matches the addresses in docs/addresses.md.
2. A line with unknown and a small number is inside our plugin. The number is the offset from the start of the plugin.
3. Find the plugin function for that offset. The build keeps the linked plugin as gvmod.elf in a folder per mod under plugin/build. List its symbols sorted by address with llvm nm. The function is the last one that starts before the offset.

Example: a crash showed unknown at 0x4514 and then main at 0x1748. The plugin offset was inside hk_end_frame. 0x1748 is inside the function that begins a draw layer. That led to the rule that the plugin only draws while the main renderer runs.


## Stale code after a rebuild

Ryujinx caches translated code on disk. It keys that cache by the names of the enabled mods. After a rebuild it can keep running old translations of the hooked functions. The install step of build.py deletes this cache. When you copy mods by hand delete games/0100c1800a9b6000/cache/cpu in the Ryujinx data folder yourself.


## Looking at main

You need the main file from your own ExeFS dump. It is an NSO file with LZ4 compressed segments. main_text in plugin/build.py shows how to unpack the code segment. For a disassembler convert main to an ELF file first. The offsets in this repository are offsets into that ELF when it starts at address 0.


## Testing without a controller

plugin/build.py knows a few test builds. Pass the name on the command line. They are only meant for development.

| Option | What it does |
| --- | --- |
| autotest | Opens the mod menu after 1200 frames |
| autoplay | Presses buttons on its own from the title screen until the player is in the open world |
| faulttest | Only together with autoplay. Makes the memory page with the camera eye read only. The next write to it crashes and the log shows which code wrote it |

The option calibrate still exists in build.py but no code uses it right now.

Do not release a build made with one of these options.
