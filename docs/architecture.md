# Architecture

## Overview

Each mod is two files in the exefs folder of a Ryujinx mod.

1. subsdk9 is our plugin. It is our own code built as an NSO module.
2. 174471C5192F8647D804BD8C733C68D0.ips is a patch for main. The file name is the build ID of main. Ryujinx only applies the patch to that exact version of the game.

plugin/build.py builds both files for both mods. GV ModMenu and GV DevMenu are built from the same source code. GV DevMenu is compiled with the define GV_DEVMENU.


## How the plugin is loaded

The game ships with the modules rtld and main and subsdk0 and subsdk1 and sdk. The rtld of the game loads every subsdk module it finds. Ryujinx adds our subsdk9 to that list. rtld then treats it like any other SDK module.

1. plugin/src/start.S holds the module header and the MOD0 block. rtld reads MOD0 to find our dynamic section and our bss.
2. rtld applies our relocations and runs gv_init in plugin/src/menu.cpp. At that point the SDK is not ready yet. gv_init only checks the patch and writes one log line.
3. From then on the plugin only runs when the game calls one of the hooked functions.

Ryujinx maps the modules back to back. Main starts 0x91A7000 bytes before our plugin. That is the size of main and subsdk0 and subsdk1 together. plugin/link.ld defines this as MAIN_DELTA. It provides the symbol __main_start. game.h adds the addresses of the game to it.

This layout is what Ryujinx does. On real hardware the layout could differ. See Known issues in the README.


## How the patch reaches the plugin

The plugin can not change the code of main at runtime. The patch does that before the game starts. It uses the IPS32 format because the code cave lies above the 16 MiB limit of plain IPS.

1. Every call of a hooked function is a bl instruction in main. The patch turns it into a bl to a slot in the code cave.
2. Each slot holds a far jump into the plugin. It is adrp and add and br on register x16. These instructions do not touch the return address.
3. The hook in the plugin does its work. It then calls the original function at its unchanged address. When the hook returns it returns straight to the caller in main.

The code cave is dead code of the game. It is a debug map handler of the batting activity called AVATAR TEST. Normal play never reaches it. The patch also turns the entry of that handler into a ret so nothing can run the cave as game code.

docs/hooks.md lists every hook and explains how to add one.


## Controller input

plugin/src/hid.cpp exports the nn::hid GetNpadState functions that main imports. rtld binds an import to the first module that defines the symbol. Our plugin is loaded before sdk. So every pad read of the game goes through us first.

The plugin forwards each call to the real function in sdk. It finds it in the symbol table of sdk. Then it hides input that belongs to the mods.

1. While the mod menu is open the game gets no input at all.
2. While ZL is held the game never sees Minus. ZL and Minus open the mod menu.
3. During noclip and the free camera the game does not see ZR and L and R and the left stick.
4. During the orbit camera and the free camera the game does not see the right stick and up and down on the D pad.


## Drawing text

The plugin draws with the font renderer of the game. The functions are listed in docs/addresses.md.

1. The text writer of the game is created at boot. In the developer boot menu that does not happen. The plugin then creates it with the init function of the game.
2. Drawing happens in the frame hook right before the game presents the frame. It uses layer 3 and the 2D view of the game.
3. The normal draw function expands a message template first. That template is not loaded during boot. So the plugin copies its text into the scratch buffer of the writer and calls the inner renderer directly.

Drawing is only safe while the main renderer of the game runs. Several activities like the dog sled race run on their own engine. They never call the camera function GET_VIEW and they have none of the 2D layers. Drawing there crashes the game. So the frame hook only draws when both of these are true.

1. GET_VIEW was called in the last 10 frames or the running module is the developer boot menu.
2. At least 60 frames passed since the running module changed.

It also only draws when there is something to show. The mod menu can not open while drawing is not allowed.


## Module changes

The game runs one module at a time. The resorts and every activity are modules. The module manager is at MODULE_MGR in game.h. The frame hook watches the running module. When it changes the plugin drops the player object and turns off noclip and switches back to the camera of the game. The old objects are freed by the game at that point.


## The two mods

| | GV ModMenu | GV DevMenu |
| --- | --- | --- |
| Define | none | GV_DEVMENU |
| Hooks | all five | debug print and frame end and GET_VIEW |
| Boots into | title menu | developer boot menu |
| Mod menu | yes | no |

Both use subsdk9 and the same code cave slots. That is why only one of them may be enabled at a time.
