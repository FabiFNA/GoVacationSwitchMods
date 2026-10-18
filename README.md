# GO VACATION Switch Mods

Mods for GO VACATION on Nintendo Switch running in Ryujinx.

This repository only contains source code. There are no keys and no game files in here. You need your own dump of the game.


## What is in this repository

1. plugin: the source code of the two mods
2. plugin/build.py: builds the two mods and installs them
3. unpack/wta_unpack.py: unpacks the model and texture archives of the game
4. docs: how the mods work inside
5. AGENTS.md: the starting point for contributors and AI assistants


## The two mods

Only one of the two mods can be enabled at a time.

GV ModMenu is an ingame mod menu. Open it with ZL + Minus.

1. Teleport to three saved positions
2. Noclip with adjustable speed
3. Orbit camera
4. Free camera with adjustable speed and an invertible Y axis
5. Progress flags and resort unlocks
6. Own all 46 collectibles
7. The developer editor of the original game
8. A radio that picks the song the resort loudspeakers play
9. Debug text of old debug modes
10. A manual position search and an info page

GV DevMenu boots the game into its hidden developer boot menu. It has no mod menu.

1. Start any activity directly
2. Set resort and time and weather and event before loading
3. Edit global data and player data


## Requirements

1. Your own dump of GO VACATION version 1.0.0 with main build ID 174471C5192F8647D804BD8C733C68D0
2. Ryujinx
3. Python 3
4. LLVM from the [LLVM releases](https://github.com/llvm/llvm-project/releases). On Windows use the installer and keep its default folder.


## What your game dump is for

Ryujinx needs your dump to run the game. Building the mods does not need it.

Two optional things use files from your dump.

1. The ExeFS for a check before building. See Check your game.
2. The RomFS for the model tool. See Look at the 3D models.

Ryujinx extracts both. Right click the game in Ryujinx and choose Extract Data. Then choose ExeFS or RomFS and pick a folder.


## Build and install

1. Install Python 3 and LLVM.
2. Download this repository and unpack it.
3. Open a terminal in the repository folder. In Windows Explorer click into the address bar of the folder. Type powershell and press Enter.
4. Run this command:

```
python plugin/build.py install
```

5. Right click the game in Ryujinx and choose Manage Mods.
6. Leave only one of GV ModMenu and GV DevMenu turned on.

If python is not found try py instead of python.

The build looks for LLVM in its default folder and in your PATH. If it prints LLVM not found tell it where the bin folder of LLVM is. Run this in the same terminal before the build. Use your own LLVM folder.

```
$env:LLVM_BIN = "D:\LLVM\bin"
```

This only lasts until you close the terminal. It is not stored in any file.

The build copies the mods into the mods folder of Ryujinx. On Windows that is inside %APPDATA%\Ryujinx. If you use Ryujinx in portable mode set RYUJINX_DIR to its portable folder the same way.

Installing also clears the Ryujinx CPU cache for this game. Ryujinx keys that cache by mod name and would otherwise keep running old code of the hooked functions.

Without install the build only puts both mods into the out folder. Copy them to mods/contents/0100c1800a9b6000 inside your Ryujinx data folder.


## Check your game

This is optional. The build checks that your game has the right version. It also checks that every patched call is where it should be.

1. Extract the ExeFS of your game with Ryujinx.
2. Create a folder called exefs inside the repository folder.
3. Put the extracted files in there. The file main must end up as exefs/main.
4. Run the build like above. It prints checked against when the check passed.

If your ExeFS is somewhere else set GAME_MAIN to its main file the same way as LLVM_BIN.


## Controls

Mod menu: ZL + Minus opens and closes. D pad moves. A selects. B goes back. Left and right change values like speeds and songs.

Orbit camera: Right stick turns and looks up and down. D pad up and down changes the distance.

Free camera: Left stick flies. Right stick looks. ZR goes up. L goes down. Hold R to go fast.

Noclip: Same controls as the free camera but it moves the player.

Radio: Left and right pick a song. A plays it on the loudspeakers right away. While the radio is on the song repeats.

Developer boot menu: D pad selects. A starts the selected module. B opens the option menu.

The player is found automatically the first time you use noclip or the orbit camera. This takes a few seconds. If another entity ends up being mistaken for your player select Player on the Player page to search again and walk around for a few seconds.


## Look at the 3D models

The unpack folder has a tool for the WTA archives of the game. The only dependency is Python 3.

You need the RomFS of your own dump. To get it right click the game in Ryujinx and click Extract Data then RomFS.

To find a model by its name (for example ufo):

```
python unpack/wta_unpack.py find path/to/romfs ufo
```

Put one model and all its textures into a folder.

```
python unpack/wta_unpack.py model path/to/romfs ani_cty_ufo_00_0 ufo
```

Open the bntx files in SwitchToolbox first and then the bfres. The textures only show up when they are loaded before the model. If the model is drawn in rainbow colors set the viewport shading of SwitchToolbox to Default.

You can also unpack a whole archive with its folders and real file names.

```
python unpack/wta_unpack.py all path/to/archive.wta outfolder
```

The model archive names its files by gear ID. The tool adds the model name to every model file. The maps are not in these archives. They are in DISC.DAT.


## How it works

Both mods are a plugin and a patch for main. The plugin is a normal SDK style module. The game's rtld loads it as subsdk9 and applies its relocations. The patch sends every call of the hooked functions to the plugin. The hooked functions themselves stay untouched and the plugin calls them when it is done. That way the patch contains no code of the game.

The Switch version still contains the developer boot menu. It is module 30 in the module table. The game normally boots module 1 which is the title menu. GV DevMenu changes two instructions in main so the game boots module 30 instead.

nnMain at 0x39C loads the module number. The check at 0x56CD58 rejects numbers above 0x1D. Both are patched to 0x1E.

The developer boot menu prints its text through a Wii style debug print function at 0xFBDEE0. On Switch that function only returns. The plugin hooks it and draws the text with the game's own font renderer. The text writer does not exist yet at that point in boot. The plugin creates it with the game's own init function at 0x71CC.

Both mods hook the end of every frame at 0x534D3C. That is where they draw. Both also hook the camera function at 0x4A858. GV DevMenu only uses it to know when it may draw. GV ModMenu runs the orbit camera and the free camera there and in the second camera function at 0x4A7F0. GV ModMenu also hooks the sound start function of nn::atk at 0x8F3DB8 for the radio.

The loudspeaker songs are stream sounds 2076 to 2195 and their rear channels 120 higher. The radio swaps the sound ID for the chosen song. To change the song right away it fades out the running one so the game starts the next song itself.

The plugin only draws while the main renderer of the game is active. Activities with their own engine never use it.


## Known issues

Only tested in Ryujinx. The plugin assumes the module layout Ryujinx uses.

Some activities like the dog sled race run on their own engine. The mod menu does not open there and the free camera does not work there.

DANCE is cut content. It has no data left and crashes.

Events that play in the open world crash when started directly from the developer menu. Set Event to FREERUN instead.

Some pages of the developer editor stay empty because the menu was never meant to be opened this way.

Back up your save before you use any of the editors.


## License

The source code is under the MIT License. See the LICENSE file.

This project is not affiliated with Bandai Namco or Nintendo. GO VACATION is a trademark of its owner.
