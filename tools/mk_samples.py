"""Build the beta package's Samples folder: one self-describing image per
loader. Each image says what it is and what a correct load looks like, so a
tester can check them by eye. Writes into dist_src\\Samples.

Needs: ImageMagick 7 (magick on PATH or the default install), Python OpenEXR,
numpy, pypng.
"""
import io, os, shutil, subprocess, zipfile
import numpy as np
import OpenEXR
import png

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "dist_src", "Samples")
MAGICK = shutil.which("magick") or r"C:\Program Files\ImageMagick-7.1.1-Q16-HDRI\magick.exe"
TMP = os.path.join(OUT, "_tmp")


def magick(*args):
    subprocess.run([MAGICK, *args], check=True)


def card(path, w, h, bg, lines, extra=()):
    """A labelled card: background colour + centred multi-line caption."""
    magick("-size", f"{w}x{h}", f"xc:{bg}", "-fill", "white", "-font", "Arial",
           "-pointsize", str(max(18, h // 14)), "-gravity", "center",
           "-annotate", "+0+0", "\n".join(lines), *extra, "PNG24:" + path)   # opaque 8-bit RGB


def read_rgb_float(path):
    """8-bit sRGB PNG -> scene-linear float32 HxWx3. EXR is linear by
    definition; storing sRGB codes would look washed out in LightWave."""
    w, h, rows, _ = png.Reader(filename=path).asRGB8()
    s = np.array([list(r) for r in rows], dtype=np.float32).reshape(h, w, 3) / 255.0
    return np.where(s <= 0.04045, s / 12.92, ((s + 0.055) / 1.055) ** 2.4).astype(np.float32)


def exr(path, header, channels):
    OpenEXR.File(header, channels).write(path)


def hdr(comp, dw=None, disp=None):
    h = {"compression": comp, "type": OpenEXR.scanlineimage}
    if dw:
        h["dataWindow"] = (np.array(dw[0], np.int32), np.array(dw[1], np.int32))
    if disp:
        h["displayWindow"] = (np.array(disp[0], np.int32), np.array(disp[1], np.int32))
    return h


os.makedirs(TMP, exist_ok=True)
t = lambda n: os.path.join(TMP, n)
o = lambda n: os.path.join(OUT, n)

# --- OpenEXR: Blender-style multilayer, DWAA (stock loader can't read DWAA) ---
card(t("comb.png"), 1024, 512, "#1f6f3f",
     ["OpenEXR multilayer (DWAA)", "Combined layer - CORRECT"])
card(t("ao.png"), 1024, 512, "#8f1f1f",
     ["WRONG LAYER", "(AO pass loaded instead of Combined)"])
c, a = read_rgb_float(t("comb.png")), read_rgb_float(t("ao.png"))
ch = {}
for k, n in enumerate("RGB"):
    ch[f"ViewLayer.Combined.{n}"] = np.ascontiguousarray(c[:, :, k])
    ch[f"ViewLayer.AO.{n}"] = np.ascontiguousarray(a[:, :, k])
ch["ViewLayer.Combined.A"] = np.ones(c.shape[:2], np.float32)
exr(o("EXR_Blender_Multilayer_DWAA.exr"), hdr(OpenEXR.DWAA_COMPRESSION), ch)

# --- OpenEXR: bounding-box crop (Nuke style): 1920x1080 format, 960x400 data ---
card(t("bbox.png"), 960, 400, "#1f4f8f",
     ["Bounding-box crop", "Image should be 1920 x 1080,", "this box centred on black"])
b = read_rgb_float(t("bbox.png"))
x0, y0 = (1920 - 960) // 2, (1080 - 400) // 2
exr(o("EXR_BBox_Crop_1920x1080.exr"),
    hdr(OpenEXR.ZIP_COMPRESSION, dw=((x0, y0), (x0 + 959, y0 + 399)), disp=((0, 0), (1919, 1079))),
    {n: np.ascontiguousarray(b[:, :, k]) for k, n in enumerate("RGB")})

# --- OpenEXR: luminance only (no RGB channels at all) ---
card(t("luma.png"), 1024, 512, "#404040", ["OpenEXR luminance only (Y)", "Should load as grey"])
y = read_rgb_float(t("luma.png")).mean(axis=2).astype(np.float32)
exr(o("EXR_Luminance_Y.exr"), hdr(OpenEXR.PIZ_COMPRESSION), {"Y": y})

# --- PNG 16-bit: a shallow gradient that bands visibly at 8 bits ---
W, H = 1024, 256
row = [int(20000 + 4000 * x / (W - 1)) for x in range(W)]
with open(o("PNG16_Smooth_Gradient.png"), "wb") as f:
    png.Writer(W, H, greyscale=True, bitdepth=16).write(f, [row] * H)

# --- WebP / AVIF (with alpha) / JPEG XL / DPX via ImageMagick ---
card(t("webp.png"), 1024, 512, "#6f3f8f", ["WebP", "loaded by APImageIO"])
magick(t("webp.png"), "-quality", "90", o("WebP_Sample.webp"))
magick("-size", "1024x512", "xc:none", "-fill", "#d07020", "-draw", "roundrectangle 40,40 984,472 60,60",
       "-fill", "white", "-font", "Arial", "-pointsize", "40", "-gravity", "center",
       "-annotate", "+0+0", "AVIF with alpha\nCorners should be transparent", t("avif.png"))
magick(t("avif.png"), "-quality", "80", o("AVIF_Alpha_Sample.avif"))
card(t("jxl.png"), 1024, 512, "#2f6f6f", ["JPEG XL", "loaded through OpenImageIO"])
magick(t("jxl.png"), "-quality", "90", o("JXL_Sample.jxl"))
card(t("dpx.png"), 1024, 512, "#5f5f1f", ["DPX (10-bit)", "loaded through OpenImageIO"])
magick(t("dpx.png"), "-depth", "10", o("DPX_Sample.dpx"))

# --- SVG: a 4:1 document, rasterised at its own size (2048 x 512) ---
with open(o("SVG_Wide_4to1.svg"), "w") as f:
    f.write('<svg xmlns="http://www.w3.org/2000/svg" width="2048" height="512" viewBox="0 0 2048 512">'
            '<rect width="2048" height="512" fill="#203a5a"/>'
            '<circle cx="256" cy="256" r="200" fill="#f0c040"/>'
            '<circle cx="1792" cy="256" r="200" fill="#f0c040"/>'
            '<text x="1024" y="230" font-family="Arial" font-size="64" fill="white" text-anchor="middle">'
            'SVG 2048 x 512</text>'
            '<text x="1024" y="320" font-family="Arial" font-size="44" fill="white" text-anchor="middle">'
            'Both circles should be round</text></svg>')

# --- Krita .kra and OpenRaster .ora: ZIP + mergedimage.png (+ a decoy thumbnail) ---
card(t("kra.png"), 1024, 512, "#7f2f5f", ["Krita / OpenRaster", "Full-size composite - CORRECT"],
     extra=())
card(t("thumb.png"), 256, 128, "#000000", ["THUMBNAIL", "(wrong)"])
merged, thumb = open(t("kra.png"), "rb").read(), open(t("thumb.png"), "rb").read()
for name, mime, doc, tn in [("Krita_Sample.kra", "application/x-krita", "maindoc.xml", "preview.png"),
                            ("OpenRaster_Sample.ora", "image/openraster", "stack.xml", "Thumbnails/thumbnail.png")]:
    with zipfile.ZipFile(o(name), "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), mime)
        z.writestr(doc, "<doc/>", compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("mergedimage.png", merged)
        z.writestr(tn, thumb, compress_type=zipfile.ZIP_DEFLATED)

shutil.rmtree(TMP)
for f in sorted(os.listdir(OUT)):
    print(f"  {f}  ({os.path.getsize(o(f))} bytes)")
