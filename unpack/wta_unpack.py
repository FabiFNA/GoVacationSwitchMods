"""Unpacks the WTA archives of GO VACATION (Switch) from your own RomFS dump.

    python wta_unpack.py all <file.wta> <outdir>
        every file of one archive, with its folder structure and real names

    python wta_unpack.py find <romfs> <text>
        lists the character, animal and vehicle models whose name contains text

    python wta_unpack.py model <romfs> <model name> <outdir>
        one model and all its textures in one folder, ready for Switch Toolbox

Only the Python standard library is used.


WTA layout (little endian)

    0x00  "WTA\\0"   u32 version
    0x08  u32 flags          0x100: every file entry is followed by a second entry with its source path
    0x0C  u32 alignment
    0x10  u32 archive size
    0x18  u32 data start
    0x1C  u32 name table offset    0x20 u32 name table size
    0x24  u32 entry table offset   0x28 u32 folder count    0x2C u32 file count

The entry table holds all folders and then all files. Every entry is 0x20 bytes and starts with
the offset of its name in the name table.

    folder  name, hash, subfolder count, file count, end of subtree, first file, parent (-1 = root), 0
    file    name, hash, alignment, size, zlib size (0 = stored), block, 0, 0
            data at data start + block * alignment

The model archive names its files by gear ID. The texture archive has one folder per gear ID with
the same name, so a model and its textures belong together through that ID. A BFRES keeps its
model name as a string pointer at 0x20.
"""
import os
import struct
import sys
import zlib

MODEL_ARCHIVE = os.path.join('Raw', 'Character', 'all_model_pack.wta')
TEXTURE_ARCHIVE = os.path.join('Raw', 'Character', 'all_model_pack_tex.wta')
EXTENSIONS = {b'FRES': '.bfres', b'BNTX': '.bntx', b'SARC': '.sarc', b'MsgS': '.msbt', b'Yaz0': '.szs'}


class Wta:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        d = self.data
        if d[:4] != b'WTA\0':
            raise ValueError(path + ' is not a WTA archive')
        flags, _, _, _, self.start, names, names_size, table, ndirs, nfiles = struct.unpack_from('<10I', d, 8)
        self.names = d[names:names + names_size]
        self.dirs = [struct.unpack_from('<8i', d, table + i * 0x20) for i in range(ndirs)]
        stride = 0x40 if flags & 0x100 else 0x20
        first = table + ndirs * 0x20
        self.files = [struct.unpack_from('<8I', d, first + i * stride) for i in range(nfiles)]

    def name(self, offset):
        return self.names[offset:self.names.index(b'\0', offset)].decode('utf-8', 'replace')

    def folder(self, index):
        parts = []
        while index >= 0:
            parts.append(self.name(self.dirs[index][0]))
            index = self.dirs[index][6]
        return '/'.join(reversed(parts))

    def entries(self):
        """(folder, name, file index) of every file"""
        for di, entry in enumerate(self.dirs):
            for fi in range(entry[5], entry[5] + entry[3]):
                yield self.folder(di), self.name(self.files[fi][0]), fi

    def read(self, index):
        _, _, align, size, zsize, block, _, _ = self.files[index]
        offset = self.start + block * align
        if zsize:
            return zlib.decompress(self.data[offset:offset + zsize])
        return self.data[offset:offset + size]


def model_name(data):
    """the model name stored in a BFRES, or None"""
    if data[:4] != b'FRES' or len(data) < 0x28:
        return None
    pointer = struct.unpack_from('<Q', data, 0x20)[0]
    if not 0 < pointer < len(data) - 2:
        return None
    length = struct.unpack_from('<H', data, pointer)[0]
    return data[pointer + 2:pointer + 2 + length].decode('ascii', 'replace') or None


def file_name(name, data):
    ext = EXTENSIONS.get(data[:4], '')
    return name if not ext or name.lower().endswith(ext) else name + ext


def unpack_all(path, out):
    wta = Wta(path)
    count = 0
    for folder, name, index in wta.entries():
        data = wta.read(index)
        target = os.path.join(out, *folder.split('/'))
        os.makedirs(target, exist_ok=True)
        model = model_name(data)
        base = file_name(name if not model else name + '_' + model, data)
        with open(os.path.join(target, base), 'wb') as f:
            f.write(data)
        count += 1
    print(count, 'files written to', os.path.abspath(out))


def models(romfs):
    """model name -> (folder, gear ID, file index) and the opened model archive"""
    wta = Wta(os.path.join(romfs, MODEL_ARCHIVE))
    found = {}
    for folder, name, index in wta.entries():
        model = model_name(wta.read(index))
        if model:
            found[model] = (folder, name, index)
    return found, wta


def find(romfs, text):
    found, _ = models(romfs)
    hits = sorted(m for m in found if text.lower() in m.lower())
    print('\n'.join(hits) if hits else 'no model name contains ' + text)


def unpack_model(romfs, model, out):
    found, wta = models(romfs)
    if model not in found:
        print(model, 'not found. Try the find command.')
        return
    folder, gear_id, index = found[model]
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, model + '.bfres'), 'wb') as f:
        f.write(wta.read(index))
    textures = Wta(os.path.join(romfs, TEXTURE_ARCHIVE))
    count = 0
    for tex_folder, name, tex_index in textures.entries():
        if tex_folder == folder + '/' + gear_id:
            data = textures.read(tex_index)
            with open(os.path.join(out, file_name(model + '_' + name, data)), 'wb') as f:
                f.write(data)
            count += 1
    print(model, 'with', count, 'texture files written to', os.path.abspath(out))
    print('Open the .bntx files in Switch Toolbox first and then the .bfres.')


def main(args):
    if len(args) == 3 and args[0] == 'all':
        unpack_all(args[1], args[2])
    elif len(args) == 3 and args[0] == 'find':
        find(args[1], args[2])
    elif len(args) == 4 and args[0] == 'model':
        unpack_model(args[1], args[2], args[3])
    else:
        print(__doc__.split('\n\n\n')[0])


if __name__ == '__main__':
    main(sys.argv[1:])
