import os, random, struct, zlib, time, sys
random.seed(2)
d = sys.argv[1]; os.makedirs(d, exist_ok=True)
def png(w, h):
    rows = b"".join(b"\0" + bytes(random.getrandbits(8) for _ in range(w*3)) for _ in range(h))
    def chunk(t, data): return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t+data) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
words = ["photo","holiday","scan","IMG","صورة","العائلة","رحلة","写真","фото","Screenshot","résumé"]
now = time.time()
for i in range(1000):
    n = "_".join(random.choice(words) for _ in range(random.randint(1,4))) + f"_{i:04d}.png"
    p = os.path.join(d, n)
    with open(p, "wb") as f: f.write(png(random.choice([64, 128, 200]), random.choice([64, 96, 150])))
    t = now - random.random()*86400*800; os.utime(p, (t, t))
