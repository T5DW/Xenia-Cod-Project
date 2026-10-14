"""Black Ops II July 20 2012 prototype (Xbox 360) fastfile container: unpack and repack.

Layout (from Default_mp-Alpha-NoWhitelistNoAC.xex and Wyatt's ui_mp.ff / common_mp.ff):

    0x000  "TAffx100" (XMem LZX blocks) or "TAff0100" (raw deflate blocks)
    0x008  u32 BE version, 0x81
    0x00C  "PHEEBs71"
    0x014  u32 BE, kept as-is
    0x018  char zoneName[32]
    0x038  u8   rsaSig[256]
    0x138  blocks: [u32 BE size][size bytes], ... , u32 0 terminator, then a trailer kept as-is

Every block is Salsa20 encrypted with the same 32-byte key as Bo1. Blocks are dealt round-robin
to 4 streams; each stream's IV is the first 8 bytes of its current 20-byte slot in a ring of 200
slots per stream, laid out [slot][stream]. The ring starts filled with the zone name, each
character written 4 times. After a block, the stream moves to its next slot and XORs the SHA-1
of the decrypted (still compressed) block into it.

Block 0 holds the 40-byte XFile header, later blocks 0xFFC0 bytes of zone each. In a TAffx100
file each block is one XMem stream (LZX, 32 KB frames); in a TAff0100 file each block is one
complete raw deflate stream. The game reads both.

The RSA signature covers the final IV ring, so every edited file fails it: the game only loads
edited fastfiles with the "Allow modified fastfiles" Xenia patch (it skips the check at
0x822450D0). Rebuilt files are always written as TAff0100.
"""

import ctypes
import struct
import sys
import zlib
from pathlib import Path

from .fastfile import KEY, FastFileError, _IvChain, _salsa20

MAGIC_LZX = b"TAffx100"
MAGIC_ZLIB = b"TAff0100"
AUTH_MAGIC = b"PHEEBs71"
VERSION = 0x81
HEADER_SIZE = 0x138
XFILE_SIZE = 40
CHUNK_SIZE = 0xFFC0
RING = 200
LZX_WINDOW_BITS = 17


def is_bo2(data: bytes) -> bool:
    return (len(data) >= 12 and data[:8] in (MAGIC_LZX, MAGIC_ZLIB)
            and struct.unpack(">I", data[8:12])[0] == VERSION)


class _Chain(_IvChain):
    def __init__(self, zone_name: bytes):
        name = zone_name.split(b"\0")[0][:31] or b"\0"
        ring = bytearray(RING * 4 * 20)
        for i in range(0, len(ring), 4):
            ring[i: i + 4] = bytes([name[(i // 4) % len(name)]]) * 4
        self.ring = ring
        self.slot = [0] * 4

    def advance(self, stream, plain_block):
        import hashlib
        digest = hashlib.sha1(plain_block).digest()
        self.slot[stream] = (self.slot[stream] + 1) % RING
        o = self._offset(stream)
        for i in range(20):
            self.ring[o + i] ^= digest[i]


_xmem = None


def _load_xmem():
    """The LZX decoder (native/xmem, built into t5menu/bin). PyInstaller unpacks it next to the
    package, so look there first."""
    global _xmem
    if _xmem is not None:
        return _xmem
    name = "xmem.dll" if sys.platform == "win32" else "libxmem.so"
    roots = [Path(__file__).resolve().parent / "bin"]
    if getattr(sys, "_MEIPASS", None):
        roots.insert(0, Path(sys._MEIPASS) / "t5menu" / "bin")
    for root in roots:
        p = root / name
        if p.is_file():
            lib = ctypes.CDLL(str(p))
            lib.xmem_decompress.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p,
                                            ctypes.c_int, ctypes.c_int]
            lib.xmem_decompress.restype = ctypes.c_int
            _xmem = lib
            return lib
    raise FastFileError(f"{name} is missing (it unpacks BO2's LZX fastfiles); "
                        "build it with native/xmem/build.sh")


def lzx_decompress(block: bytes, out_size: int = 0x10000) -> bytes:
    lib = _load_xmem()
    out = ctypes.create_string_buffer(out_size)
    n = lib.xmem_decompress(block, len(block), out, out_size, LZX_WINDOW_BITS)
    if n < 0:
        raise FastFileError(f"LZX block failed to unpack (error {n})")
    return out.raw[:n]


class BO2FastFile:
    """Same interface as fastfile.FastFile: .zone, .zone_name, .signed, build()."""

    game = "bo2"

    def __init__(self, header: bytes, zone: bytes, blocks, trailer: bytes):
        self.header = bytearray(header)
        self.zone = zone
        self.original_zone = zone
        self.blocks = blocks  # [(zone start, size, plain block)] when the file was TAff0100
        self.trailer = trailer

    @property
    def signed(self) -> bool:
        return True  # every file carries the signature, see the module docstring

    @property
    def zone_name(self) -> str:
        return bytes(self.header[0x18:0x38]).split(b"\0")[0].decode("latin1")

    @property
    def compression(self) -> str:
        return "LZX" if bytes(self.header[:8]) == MAGIC_LZX else "zlib"

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            return cls.parse(f.read())

    @classmethod
    def parse(cls, data: bytes):
        if len(data) < HEADER_SIZE or not is_bo2(data):
            raise FastFileError("not a Black Ops II July 20 prototype fastfile")
        if data[0x0C:0x14] != AUTH_MAGIC:
            raise FastFileError(f"unexpected auth header magic {data[0x0C:0x14]!r}")
        lzx = data[:8] == MAGIC_LZX
        chain = _Chain(data[0x18:0x38])
        out, blocks = [], []
        pos = 0
        off = HEADER_SIZE
        index = 0
        while off + 4 <= len(data):
            size = struct.unpack(">I", data[off: off + 4])[0]
            if size == 0:
                off += 4
                break
            block = data[off + 4: off + 4 + size]
            if len(block) != size:
                raise FastFileError(f"truncated block {index} at 0x{off:X}")
            off += 4 + size
            s = index % 4
            plain = _salsa20(KEY, chain.iv(s), block)
            chain.advance(s, plain)
            if lzx:
                chunk = lzx_decompress(plain)
            else:
                inflater = zlib.decompressobj(-15)
                try:
                    chunk = inflater.decompress(plain)
                except zlib.error as e:
                    raise FastFileError(f"block {index} failed to inflate: {e}") from None
                blocks.append((pos, len(chunk), plain))
            out.append(chunk)
            pos += len(chunk)
            index += 1
        zone = b"".join(out)
        if len(zone) < XFILE_SIZE:
            raise FastFileError("zone is empty")
        expected = struct.unpack(">I", zone[:4])[0] + XFILE_SIZE
        if expected != len(zone):
            raise FastFileError(f"zone size mismatch: XFile says {expected}, got {len(zone)}")
        return cls(data[:HEADER_SIZE], zone, None if lzx else blocks, data[off:])

    def build(self, signed: bool = False, level: int = 6) -> bytes:
        """Repack as TAff0100. Blocks of a TAff0100 original that didn't change are kept as they
        were (the IVs still have to be recomputed, since they chain). `signed` is ignored: the
        old signature stays in the header, and the Xenia patch makes the game skip it."""
        zone = bytearray(self.zone)
        struct.pack_into(">I", zone, 0, len(zone) - XFILE_SIZE)
        old = {start: (size, plain) for start, size, plain in (self.blocks or [])}
        chunks = [(0, XFILE_SIZE)]
        for i in range(XFILE_SIZE, len(zone), CHUNK_SIZE):
            chunks.append((i, min(CHUNK_SIZE, len(zone) - i)))
        header = bytearray(self.header)
        header[:8] = MAGIC_ZLIB
        chain = _Chain(bytes(header[0x18:0x38]))
        out = bytearray(header)
        original = self.original_zone
        for index, (start, size) in enumerate(chunks):
            data = bytes(zone[start: start + size])
            kept = old.get(start)
            if kept and kept[0] == size and original[start: start + size] == data:
                plain = kept[1]
            else:
                c = zlib.compressobj(level, zlib.DEFLATED, -15)
                plain = c.compress(data) + c.flush()
            s = index % 4
            out += struct.pack(">I", len(plain)) + _salsa20(KEY, chain.iv(s), plain)
            chain.advance(s, plain)
        out += b"\0\0\0\0"
        out += self.trailer
        return bytes(out)
