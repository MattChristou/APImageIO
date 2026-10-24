"""Generate Krita .kra / OpenRaster .ora regression files for krabridge.

Each positive case also gets <name>.expect = the RGBA decode of its
mergedimage.png by an independent decoder (pypng): 8-bit cases as RGBA8,
16-bit cases as big-endian RGBA16. The harness compares byte-for-byte.
"""
import io, os, struct, zipfile
import png  # pypng

W, H = 64, 40

def pixels(depth, planes, alpha_ramp=True):
    """Asymmetric content: gradient + a red block top-left (orientation check)."""
    mx = (1 << depth) - 1
    rows = []
    for y in range(H):
        row = []
        for x in range(W):
            r = mx if (x < 10 and y < 6) else (x * mx) // (W - 1)
            g = (y * mx) // (H - 1)
            b = mx - r
            a = ((x * mx) // (W - 1)) if alpha_ramp else mx
            grey = (r + g + b) // 3
            px = {1: [grey], 2: [grey, a], 3: [r, g, b], 4: [r, g, b, a]}[planes]
            row.extend(px)
        rows.append(row)
    return rows

def png_bytes(depth, planes, palette=False):
    buf = io.BytesIO()
    if palette:
        pal = [(i, 255 - i, (i * 7) & 255) for i in range(256)]
        rows = [[(x + y) & 255 for x in range(W)] for y in range(H)]
        png.Writer(W, H, palette=pal, bitdepth=8).write(buf, rows)
    else:
        greyscale = planes in (1, 2)
        alpha = planes in (2, 4)
        png.Writer(W, H, greyscale=greyscale, alpha=alpha, bitdepth=depth).write(buf, pixels(depth, planes))
    return buf.getvalue()

def expect_bytes(data, depth):
    """Independent RGBA decode, as the harness dumps it."""
    w, h, rows, info = png.Reader(bytes=data).asRGBA()
    out = bytearray()
    for row in rows:
        if depth == 16:
            out += struct.pack(">%dH" % len(row), *row)
        else:
            out += bytes(row)
    return bytes(out)

def container(name, merged, comp=zipfile.ZIP_STORED, mime="application/x-krita",
              member="mergedimage.png", preview=True):
    krita = "krita" in mime
    with zipfile.ZipFile(name, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), mime)       # stored, first
        z.writestr("maindoc.xml" if krita else "stack.xml", "<doc/>", compress_type=zipfile.ZIP_DEFLATED)
        if merged is not None:
            z.writestr(member, merged, compress_type=comp)
        if preview:
            thumb = png_bytes(8, 3)
            z.writestr("preview.png" if krita else "Thumbnails/thumbnail.png", thumb,
                       compress_type=zipfile.ZIP_DEFLATED)

def positive(name, depth, planes, palette=False, **kw):
    data = png_bytes(depth, planes, palette)
    container(name, data, **kw)
    open(name + ".expect", "wb").write(expect_bytes(data, 16 if depth == 16 else 8))

os.makedirs("data", exist_ok=True)
os.chdir("data")
positive("k_rgba8.kra", 8, 4)
positive("k_rgba8_deflate.kra", 8, 4, comp=zipfile.ZIP_DEFLATED)
positive("k_rgb8.kra", 8, 3)
positive("k_rgba16.kra", 16, 4, comp=zipfile.ZIP_DEFLATED)
positive("k_rgb16.kra", 16, 3)
positive("k_gray8.kra", 8, 1)
positive("k_graya8.kra", 8, 2)
positive("k_palette.kra", 8, 3, palette=True)
positive("k_rgba8.ora", 8, 4, comp=zipfile.ZIP_DEFLATED, mime="image/openraster")
positive("K_UPPER.KRA", 8, 3)
positive("k_case.kra", 8, 3, member="MergedImage.PNG")
container("k_nomerged.kra", None)                                  # BADFILE, never preview
container("k_notpng.kra", b"definitely not a png")                 # BADFILE
open("k_fake.kra", "wb").write(b"not a zip at all")                # NOREC
open("k_empty.kra", "wb").close()                                  # NOREC
import shutil; shutil.copy("k_rgba8.kra", "k_zipnotkra.zip")      # NOREC (extension)
print("kra ok")
