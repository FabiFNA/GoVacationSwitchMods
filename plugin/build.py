"""Builds the two GO VACATION mods. Each one is a plugin plus a patch for main:

  GV ModMenu    the mod menu (teleport, noclip, cameras, progress, save editor, radio)
  GV DevMenu    boots into the developer boot menu of the game and shows its text

  subsdk9       our code as NSO (loaded by the game's rtld like any other SDK module)
  <main>.ips    patches main: every call of a hooked function goes to a code cave, the cave jumps into subsdk9

Both mods use subsdk9 and the same code cave, so only one of them can be enabled at a time.

usage: python build.py            build both into ../out
       python build.py install    build and copy both into the Ryujinx mods folder (then disable one in Ryujinx)
Needs clang, ld.lld and llvm-nm from LLVM. They are looked for in LLVM_BIN, ../tools/llvm/bin, the default install
folder of the LLVM installer and the PATH, in that order.
No game code is stored here and none ends up in the mods: the patches only contain branches computed here and
a few instructions of our own. If GAME_MAIN points to the main file of your ExeFS dump (default: ../exefs/main)
the build also checks that main is the supported version and that every patched call is where it is expected.
"""
import hashlib, os, shutil, struct, subprocess, sys

EXE = '.exe' if os.name == 'nt' else ''
ARGS = {a.lstrip('-') for a in sys.argv[1:]}

HERE = os.path.dirname(os.path.abspath(__file__))


def find_llvm():
    """the first folder that has all LLVM tools we need"""
    tools = ['clang' + EXE, 'ld.lld' + EXE, 'llvm-nm' + EXE]
    places = [os.environ.get('LLVM_BIN'), os.path.join(HERE, '..', 'tools', 'llvm', 'bin')]
    if os.name == 'nt':
        places += [os.path.join(os.environ.get(v, ''), 'LLVM', 'bin') for v in ('ProgramFiles', 'ProgramFiles(x86)')]
    else:
        places += ['/usr/bin', '/usr/local/bin', '/opt/homebrew/opt/llvm/bin', '/usr/local/opt/llvm/bin']
    clang = shutil.which('clang')
    if clang:
        places.append(os.path.dirname(clang))
    for place in places:
        if place and all(os.path.isfile(os.path.join(place, t)) for t in tools):
            return place
    sys.exit('LLVM not found. Install LLVM or set LLVM_BIN to the bin folder that has clang, ld.lld and llvm-nm.')


LLVM = None  # set by main()
GAME_MAIN = os.environ.get('GAME_MAIN', os.path.join(HERE, '..', 'exefs', 'main'))
BUILD = os.path.join(HERE, 'build')
OUT = os.path.join(HERE, '..', 'out')
TITLE_ID = '0100c1800a9b6000'
MAIN_BUILD_ID = '174471C5192F8647D804BD8C733C68D0'
RYUJINX_DIR = os.environ.get('RYUJINX_DIR') or os.path.join(os.environ.get('APPDATA', os.path.expanduser('~/.config')), 'Ryujinx')
RYUJINX_MODS = os.path.join(RYUJINX_DIR, 'mods', 'contents', TITLE_ID)

MAIN_DELTA = 0x91A7000        # must match link.ld
CAVE_BASE = 0xCFB780          # must match game.h; dead code: phase-0 handler of the BATTING 'AVATAR TEST' debug map
CAVE_END = 0xCFBFC0          # whole AVATAR TEST body up to the MOB VIEWER function
NSO_HEADER = 0x100

SOURCES = ['src/start.S', 'src/menu.cpp', 'src/hid.cpp', 'src/scanner.cpp', 'src/player.cpp', 'src/camera.cpp', 'src/devmenu.cpp', 'src/radio.cpp']
CFLAGS = ['--target=aarch64-none-elf', '-march=armv8-a', '-mtune=cortex-a57', '-O2', '-fPIC', '-ffreestanding',
          '-fno-exceptions', '-fno-rtti', '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
          '-fno-stack-protector', '-fno-threadsafe-statics', '-fvisibility=hidden', '-nostdlib', '-std=c++20',
          '-Wall', '-Wno-unused-function']


def run(*cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit(f"failed: {' '.join(cmd[:2])}")
    if r.stderr.strip():
        print(r.stderr.strip())
    return r.stdout


# ---------------------------------------------------------------- A64 encoders
def b(pc, target):
    off = (target - pc) >> 2
    assert -(1 << 25) <= off < (1 << 25)
    return 0x14000000 | (off & 0x3FFFFFF)

def adrp(rd, pc, target):
    imm = ((target & ~0xFFF) - (pc & ~0xFFF)) >> 12
    return 0x90000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5) | rd

def add_imm(rd, rn, imm12):
    return 0x91000000 | (imm12 << 10) | (rn << 5) | rd

BR_X16 = 0xD61F0200

def far_jump(pc, target):
    """adrp x16 / add x16 / br x16 (16 bytes incl. padding)"""
    return [adrp(16, pc, target), add_imm(16, 16, target & 0xFFF), BR_X16, 0xD503201F]


# ---------------------------------------------------------------- the game's main (from the user's dump)
def lz4_block(src, size):
    """LZ4 block decompression (the format NSO segments use)"""
    out, i = bytearray(), 0
    while i < len(src):
        token = src[i]; i += 1
        n = token >> 4
        if n == 15:
            while True:
                b = src[i]; i += 1; n += b
                if b != 255:
                    break
        out += src[i:i + n]; i += n
        if i >= len(src):
            break
        dist = src[i] | src[i + 1] << 8; i += 2
        n = token & 15
        if n == 15:
            while True:
                b = src[i]; i += 1; n += b
                if b != 255:
                    break
        n += 4
        start = len(out) - dist
        if dist >= n:
            out += out[start:start + n]
        else:
            for k in range(n):
                out.append(out[start + k])
    assert len(out) == size, 'bad LZ4 data in main'
    return bytes(out)


def main_text(path):
    """code segment of the game's main NSO, after checking it is the supported version"""
    d = open(path, 'rb').read()
    if d[:4] != b'NSO0':
        sys.exit(f'{path} is not an NSO file')
    build_id = d[0x40:0x50].hex().upper()
    if build_id != MAIN_BUILD_ID:
        sys.exit(f'main has build ID {build_id} but these mods are made for {MAIN_BUILD_ID} (game version 1.0.0)')
    flags, = struct.unpack_from('<I', d, 0x0C)
    offset, _, size, _ = struct.unpack_from('<IIII', d, 0x10)
    stored, = struct.unpack_from('<I', d, 0x60)
    return lz4_block(d[offset:offset + stored], size) if flags & 1 else d[offset:offset + size]


# ---------------------------------------------------------------- ELF -> NSO
def elf_segments(path):
    d = open(path, 'rb').read()
    phoff, = struct.unpack_from('<Q', d, 0x20)
    phnum, = struct.unpack_from('<H', d, 0x38)
    segs = []
    for i in range(phnum):
        t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', d, phoff + i * 56)
        if t == 1:
            segs.append((va, d[off:off + fsz], msz))
    return segs


def build_nso(elf, out):
    (tva, text, _), (rva, ro, _), (dva, data, dmsz) = elf_segments(elf)
    assert tva == 0 and rva % 0x1000 == 0 and dva % 0x1000 == 0, (hex(tva), hex(rva), hex(dva))
    bss = dmsz - len(data)
    build_id = hashlib.sha256(text + ro + data).digest()[:0x20]
    h = bytearray(NSO_HEADER)
    struct.pack_into('<4sIII', h, 0, b'NSO0', 0, 0, 0)  # flags 0: uncompressed, no hash check
    offs = [NSO_HEADER, NSO_HEADER + len(text), NSO_HEADER + len(text) + len(ro)]
    struct.pack_into('<IIII', h, 0x10, offs[0], tva, len(text), 0)
    struct.pack_into('<IIII', h, 0x20, offs[1], rva, len(ro), 0)
    struct.pack_into('<IIII', h, 0x30, offs[2], dva, len(data), bss)
    h[0x40:0x60] = build_id
    struct.pack_into('<III', h, 0x60, len(text), len(ro), len(data))
    for i, seg in enumerate((text, ro, data)):
        h[0xA0 + i * 0x20:0xC0 + i * 0x20] = hashlib.sha256(seg).digest()
    with open(out, 'wb') as f:
        f.write(h + text + ro + data)
    return dva + dmsz


def symbols(elf):
    out = {}
    for line in run(os.path.join(LLVM, 'llvm-nm' + EXE), elf).splitlines():
        parts = line.split()
        if len(parts) == 3:
            out[parts[2]] = int(parts[0], 16)
    return out


# ---------------------------------------------------------------- main IPS
def ips(patches):
    """IPS32 (4-byte offsets) - the code cave lies above the 16 MiB limit of plain IPS."""
    buf = bytearray(b'IPS32')
    for addr, data in sorted(patches):
        off = addr + NSO_HEADER
        assert off != 0x45454F46  # 'EEOF'
        buf += off.to_bytes(4, 'big') + len(data).to_bytes(2, 'big') + data
    return bytes(buf + b'EEOF')


def words(ws):
    return b''.join(struct.pack('<I', w) for w in ws)


# hook -> (cave slot, hooked function, its call sites). Every call site is a `bl` to the function; the patch turns
# it into a `bl` to the hook's cave slot and the hook calls the untouched function itself. Without call sites the
# function starts with the branch instead: only DEBUG_PRINT, which just returns on Switch and is never called by us.
# The slots must match game.h (hook_entry), slot 0 is checked by gv_init.
HOOKS = {
    'hk_debug_print': (0, 0xFBDEE0, None),
    'hk_end_frame':   (1, 0x534D3C, [0x000DC4, 0xBDA5B4, 0xBDACEC, 0xCF1BEC, 0xFB9984, 0xFF6B10]),
    'hk_get_view':    (2, 0x04A858, [0x045B7C, 0x049C58, 0x0898B0, 0x08AFDC, 0x0F07E8, 0x189D90, 0x222B08]),
    'hk_set_view':    (3, 0x04A7F0, [0x1B4224, 0x22210C]),
    'hk_setup_sound': (4, 0x8F3DB8, [0x8F24D4, 0x8F251C, 0x8F2568, 0x8F3DA8]),  # nn::atk SoundArchivePlayer::detail_SetupSoundImpl
}
AVATAR_TEST_DISPATCH = 0xCFB754  # neutralised so nothing can ever execute our cave as game code

# Bug fixes inside the original developer editor (CMenuSelect). Its "activities" page (FUN_00caa418) sets its
# cursor to -1 when its activity list is missing and then indexes the flag array with it (crash on left/right).
# Both places get a cursor of 0 instead.
GAME_FIXES = [
    (0xCAAAC8, 0x2A1F03F6),  # mov w22, wzr
    (0xCAABC4, 0x2A1F03E8),  # mov w8, wzr
]

# Boot into the developer boot menu (module 30) instead of the title menu (module 1).
DEV_BOOT = [
    (0x00039C, 0x528003C0),  # nnMain: mov w0, #0x1e
    (0x56CD58, 0x71007A7F),  # FUN_0056cd48: cmp w19, #0x1e  (accept module numbers up to 30)
]

MODS = {
    'GV ModMenu': {'define': None, 'dev_boot': False,
                   'hooks': ['hk_debug_print', 'hk_end_frame', 'hk_get_view', 'hk_set_view', 'hk_setup_sound']},
    # the developer menu build only draws the game's debug text; get_view tells it when that is safe
    'GV DevMenu': {'define': 'GV_DEVMENU', 'dev_boot': True,
                   'hooks': ['hk_debug_print', 'hk_end_frame', 'hk_get_view']},
}


def bl(pc, target):
    return 0x80000000 | b(pc, target)


def verify_main(text):
    """every call site we patch must be a `bl` to the hooked function in the user's main"""
    for name, (_, func, sites) in HOOKS.items():
        for site in sites or []:
            insn, = struct.unpack_from('<I', text, site)
            if insn != bl(site, func):
                sys.exit(f'{name}: no call of {func:#x} at {site:#x}, is this the right main?')


def main_patches(mod, sym):
    p = [(AVATAR_TEST_DISPATCH, words([0xD65F03C0]))]  # ret
    p += [(addr, words([insn])) for addr, insn in GAME_FIXES]
    if mod['dev_boot']:
        p += [(addr, words([insn])) for addr, insn in DEV_BOOT]
    for name in mod['hooks']:
        slot, func, sites = HOOKS[name]
        entry = CAVE_BASE + slot * 0x10
        assert entry + 0x10 <= CAVE_END
        p.append((entry, words(far_jump(entry, MAIN_DELTA + sym[name]))))
        if sites is None:
            p.append((func, words([b(func, entry)])))
        else:
            p += [(site, words([bl(site, entry)])) for site in sites]
    return p


def build_mod(name, mod):
    build = os.path.join(BUILD, name.replace(' ', '_'))
    os.makedirs(build, exist_ok=True)
    clang = os.path.join(LLVM, 'clang' + EXE)
    defines = ['-D' + mod['define']] if mod['define'] else []
    defines += ['-DGV_' + f.upper() for f in ('autotest', 'calibrate', 'autoplay', 'faulttest') if f in ARGS]
    objs = []
    for src in SOURCES:
        obj = os.path.join(build, os.path.basename(src) + '.o')
        run(clang, *CFLAGS, *defines, '-c', os.path.join(HERE, src), '-o', obj)
        objs.append(obj)
    elf = os.path.join(build, 'gvmod.elf')
    run(os.path.join(LLVM, 'ld.lld' + EXE), '-shared', '-T', os.path.join(HERE, 'link.ld'), '--hash-style=sysv',
        '-init=gv_init', '-z', 'max-page-size=0x1000', '-z', 'noexecstack', '-z', 'norelro', '--no-undefined',
        '-Bsymbolic', '--pack-dyn-relocs=none', '--build-id=none', '-o', elf, *objs)
    sym = symbols(elf)
    size = build_nso(elf, os.path.join(build, 'subsdk9'))
    assert size <= 0x8000000

    out = os.path.join(OUT, name)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, 'exefs'))
    shutil.copy(os.path.join(build, 'subsdk9'), os.path.join(out, 'exefs', 'subsdk9'))
    with open(os.path.join(out, 'exefs', MAIN_BUILD_ID + '.ips'), 'wb') as f:
        f.write(ips(main_patches(mod, sym)))
    print(f'built {name}: subsdk9 {size:#x} bytes')
    return out


def main():
    global LLVM
    LLVM = find_llvm()
    print('using LLVM in', LLVM)
    if os.path.isfile(GAME_MAIN):
        verify_main(main_text(GAME_MAIN))
        print('checked against', GAME_MAIN)
    built = {name: build_mod(name, mod) for name, mod in MODS.items()}

    if 'install' in ARGS:
        for name, path in built.items():
            dst = os.path.join(RYUJINX_MODS, name)
            if os.path.isdir(dst):
                shutil.rmtree(dst)
            shutil.copytree(path, dst)
            print('installed', dst)
        print('Only one of the two mods may be enabled. Turn the other one off in Manage Mods in Ryujinx.')
        # Ryujinx keys its PPTC translation cache by the mod *names*, so a rebuilt plugin would keep running the
        # stale translations of hooked functions. Drop the cache; it is rebuilt on the next start.
        cache = os.path.join(RYUJINX_DIR, 'games', TITLE_ID, 'cache', 'cpu')
        if os.path.isdir(cache):
            shutil.rmtree(cache, ignore_errors=True)
            print('purged PPTC cache')


if __name__ == '__main__':
    main()
