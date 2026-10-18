# Features

This file explains how each feature works inside. The README explains how to use them.


## Mod menu

plugin/src/menu.cpp holds the menu. Each page is a Page with a build function. The build function adds one Item per line. An item can have an action for A and an action for left and right.

The menu reads input in the frame hook. It draws in the same hook. See Drawing text in docs/architecture.md.


## Finding the player

Noclip and teleport and the orbit camera need the position of the player. The game keeps it in a Havok motion object. Its address changes every time a module loads. plugin/src/scanner.cpp finds it automatically.

1. The game keeps a copy of the player position at REF_POS. The scanner collects every float triple in memory that equals this copy. It only keeps the ones inside a Havok motion object. Those start with MOTION_VTABLE.
2. It first searches only the memory region where the player was found last time. If nothing is there it searches all memory. The work is spread over many frames so the game keeps running smoothly.
3. It skips memory that the GPU uses. Reading that memory makes Ryujinx drop its resolution scaling.
4. Balls and props use the same Havok class. The player body always stays upright. So the scanner only keeps motion objects without rotation.
5. The camera of the game looks at the player. The scanner picks the upright object closest to the view ray of the camera.
6. If no object is close to the ray it moves each candidate a little and watches whether the camera follows.
7. While you walk with the camera of the game the scanner checks the pick. The player is the object that moves the way the left stick points. The scanner only switches when the current pick does not follow the stick and exactly one other object clearly does. The Player item in the menu shows confirmed or unsure.

A new module frees the old object. The plugin then drops it. See Module changes in docs/architecture.md. The menu also has a manual position search for cases where the automatic one fails.


## Noclip and teleport

plugin/src/player.cpp moves the player by writing the position of the Havok motion object. Noclip writes a new position every frame. The game then has no say in where the player goes. Teleport writes one of three saved positions once.


## Orbit camera and free camera

plugin/src/camera.cpp hooks both camera functions.

1. GET_VIEW returns the view matrix of a camera. For the main camera the hook returns our own matrix instead.
2. SET_VIEW stores a new view matrix. For the main camera the hook stores our own matrix instead.

The orbit camera circles around the player position. The free camera has its own position and direction. Both build a look at matrix from that. The camera of the game keeps running in the background. Its last matrix is kept in g_game_view.


## Progress and collectibles

The menu writes the save data of the current player in memory. The layout is in docs/addresses.md. The game saves it the next time it saves on its own.


## Developer editor

plugin/src/devmenu.cpp opens the developer editor that is still in the game but never used. Nothing in the release build creates it. The plugin builds a small menu launcher object like the game does for its other menus. The factory of that object creates the editor. The plugin then starts it and updates it once per frame. The editor draws itself and reads the pad state of the game.

Some pages stay empty. They expect data that only exists in a development build.


## Radio

plugin/src/radio.cpp hooks SETUP_SOUND. Every sound of the game starts there with its sound ID.

1. The loudspeaker songs are stream sounds 2076 to 2195. These are the front channels. Several playlists reuse the same 20 songs.
2. Each front channel has a rear channel 120 IDs higher.
3. While the radio is on the hook swaps every loudspeaker song for a sound ID of the chosen song. SONG_OF in radio.cpp maps each front ID to its song. It was read from the sound archive of the game.
4. Play now stops the running loudspeaker songs. The playlist logic of the game then starts the next song. The hook swaps that one too.


## Developer boot menu

The Switch version still has the developer boot menu of the Wii version. It is module 30. GV DevMenu changes two instructions so the game boots it instead of the title menu.

The menu prints its text with the debug print function of the Wii version. On Switch that function only returns. The plugin hooks it and stores up to 512 lines per frame. The frame hook draws them. The text writer does not exist yet in the boot menu. The plugin creates it with the init function of the game.


## Debug text

Old debug modes of the game also print with the debug print function. GV ModMenu has an item that turns showing that text on and off.
