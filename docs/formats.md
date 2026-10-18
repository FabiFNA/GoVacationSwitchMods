# File formats

## WTA archives

The game keeps many files in WTA archives. The models of characters and animals and vehicles are in Raw/Character/all_model_pack.wta in the RomFS. Their textures are in all_model_pack_tex.wta next to it. The texts of the game are in WTA archives in the MessageStudio folder.

unpack/wta_unpack.py reads them. Its docstring has the full layout. In short:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | WTA and a zero byte |
| 0x08 | 4 | Flags. 0x100 means every file entry is followed by a second entry with its source path |
| 0x0C | 4 | Alignment |
| 0x10 | 4 | Size of the archive |
| 0x18 | 4 | Start of the data |
| 0x1C | 4 | Offset of the name table |
| 0x20 | 4 | Size of the name table |
| 0x24 | 4 | Offset of the entry table |
| 0x28 | 4 | Number of folders |
| 0x2C | 4 | Number of files |

The entry table holds all folders and then all files. Every entry is 0x20 bytes. It starts with the offset of its name in the name table.

A folder entry has its name and a hash and the number of subfolders and the number of files and the end of its subtree and its first file and its parent. The parent is minus 1 for the root.

A file entry has its name and a hash and an alignment and its size and its zlib size and a block number. The data starts at the data start plus the block number times the alignment. A zlib size of 0 means the file is stored without compression.

The model archive names its files by gear ID. The gear IDs are the same as in GearDataBase. The texture archive has one folder per gear ID. A model and its textures belong together through that ID. A BFRES keeps its model name as a string pointer at 0x20.

The files inside are standard Switch formats. Models are BFRES. Textures are BNTX. Texts are MSBT.


## DISC.DAT

The maps and most other data are in DISC.DAT in the RomFS. It has no name table. Its table of contents is inside main. Without it the file is a row of zlib blocks. Each block can be found by its zlib header and identified by the magic of its content.

Maps are blocks with the magic MAP. Each holds a BFRES with the map models and textures and some data of the game. Most maps exist in two or three versions for different times of day.
