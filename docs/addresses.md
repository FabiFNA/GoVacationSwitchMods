# Addresses

All addresses are offsets from the start of main. They are only valid for main with build ID 174471C5192F8647D804BD8C733C68D0. That is GO VACATION version 1.0.0.

Names like FUN_00caa418 in the code are the default names a disassembler gives a function. They only mean the function at that address.


## Hooked functions

| Address | Name in code | What it is |
| --- | --- | --- |
| 0x534D3C | END_FRAME | Called once per frame right before the frame is shown |
| 0xFBDEE0 | DEBUG_PRINT | Debug text of the Wii version. Takes x and y and color and text. Only returns on Switch |
| 0x04A7F0 | SET_VIEW | Stores the view matrix of a camera object |
| 0x04A858 | GET_VIEW | Returns the view matrix of a camera object |
| 0x8F3DB8 | SETUP_SOUND | nn::atk SoundArchivePlayer detail_SetupSoundImpl. Every sound starts here |


## Text drawing

| Address | What it is |
| --- | --- |
| 0x6F80 | Returns the text writer |
| 0x71CC | Creates the text writer and loads the font |
| 0x1714 | Begins a draw layer |
| 0x1778 | Ends the draw layer |
| 0x17D4 | Sets the 2D view |
| 0x7C48 | Begins text output |
| 0x7C80 | Ends text output |
| 0xB99C | Sets the text of the writer |
| 0xA67C | Draws text through a message template |
| 0x8694 | Inner text renderer used by the plugin |
| 0x568A390 | Pointer to the message and font manager. Text is only safe when it is set |
| 0x1024D70 | vsnprintf of the game |


## Game state

| Address | Name in code | What it is |
| --- | --- | --- |
| 0x7C4D308 | MODULE_MGR | Module manager. Plus 0x30 is the requested module. Plus 0x34 is the running module |
| 0x59B2F20 | MAIN_CAMERA | First of 10 camera objects of 0x160 bytes each |
| 0x7C4C688 | PLAYER_DATA_PTR | Pointer to the save data of all players |
| 0x55BF68 | | Returns the current player slot |
| 0x7CB5800 | REF_POS | A copy of the player position that the game updates every frame |
| 0x5305A78 | MOTION_VTABLE | Vtable of the Havok motion objects. The player body is one of them |

Camera object: the eye is at plus 0x70. The 3x4 view matrix is at plus 0xAC. The target is at plus 0xF0.

Player save data: each player slot is 0x13160 bytes. The data starts at plus 0x5E60. The 46 collectibles start at plus 0xC74 with one byte each. Bit 0 means owned. The 19 progress flags start at plus 0xCA4. Bit 0 means unlocked. Bits 1 and 2 hold the new state.

Havok motion object: the position used by the plugin is at plus 0x70. The orientation is at plus 0x90.


## Patch locations

| Address | What it is |
| --- | --- |
| 0xCFB780 to 0xCFBFC0 | Code cave. Dead code of the AVATAR TEST debug map |
| 0xCFB754 | Entry of that dead code |
| 0x00039C | nnMain loads the module number to boot |
| 0x56CD58 | Check that rejects module numbers above 29 |
| 0xCAAAC8 and 0xCAABC4 | Cursor setup of the activity page in the developer editor |


## Developer editor

| Address | What it is |
| --- | --- |
| 0xCA8088 | Menu factory of the game |
| 0xCAEDCC | Example of a place that builds a menu |
| 0xCAA418 | Activity page of the developer editor |

The developer editor is System Menu CMenuSelect with menu ID 0x16. plugin/src/devmenu.cpp builds it the same way the game builds its other menus.


## Modules

The module numbers are listed in kModuleNames in plugin/src/menu.cpp. Module 0 is the open world. Module 1 is the title menu. Module 30 is the developer boot menu. Module 29 is DANCE which has no data left.
