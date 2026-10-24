"""Generate EXR regression files for exrbridge. Each file encodes its
expected result in solid values so the harness can check what was chosen."""
import numpy as np
import OpenEXR
import os

os.makedirs("data", exist_ok=True)
os.chdir("data")

W, H = 64, 32

def solid(v, w=W, h=H):
    return np.full((h, w), v, dtype=np.float32)

def hdr(dw=None, disp=None, comp=OpenEXR.ZIP_COMPRESSION):
    h = {"compression": comp, "type": OpenEXR.scanlineimage}
    if dw:
        h["dataWindow"] = (np.array(dw[0], dtype=np.int32), np.array(dw[1], dtype=np.int32))
    if disp:
        h["displayWindow"] = (np.array(disp[0], dtype=np.int32), np.array(disp[1], dtype=np.int32))
    return h

def write(name, header, chans):
    OpenEXR.File(header, chans).write(name)

# 1. Blender-style: no default layer; AO sorts before Combined. Expect Combined (R=0.5).
write("blender_ao_combined.exr", hdr(), {
    "ViewLayer.AO.R": solid(0.9), "ViewLayer.AO.G": solid(0.9), "ViewLayer.AO.B": solid(0.9),
    "ViewLayer.Combined.R": solid(0.5), "ViewLayer.Combined.G": solid(0.25),
    "ViewLayer.Combined.B": solid(0.125), "ViewLayer.Combined.A": solid(1.0)})

# 2. Luminance only. Expect grey 0.3, no alpha.
write("y_only.exr", hdr(), {"Y": solid(0.3)})

# 3. Luminance + alpha. Expect grey 0.3, alpha 0.6.
write("y_alpha.exr", hdr(), {"Y": solid(0.3), "A": solid(0.6)})

# 4. Single custom channel. Expect grey 7.0 (depth-like values pass through).
write("z_only.exr", hdr(), {"Z": solid(7.0)})

# 5. bbox crop: 16x8 data window at (10,4) inside a 64x32 format.
#    Expect 64x32 output, R=1 inside the box, 0 outside.
dw = ((10, 4), (25, 11))
write("bbox_crop.exr", hdr(dw=dw, disp=((0, 0), (W - 1, H - 1))), {
    "R": solid(1.0, 16, 8), "G": solid(1.0, 16, 8), "B": solid(1.0, 16, 8)})

# 6. overscan: data window 8px larger on every side than the 64x32 format.
#    Data is 1.0 in the overscan ring, 0.5 inside the format. Expect 64x32, all 0.5.
ow, oh = W + 16, H + 16
r = np.full((oh, ow), 1.0, dtype=np.float32)
r[8:8 + H, 8:8 + W] = 0.5
write("overscan.exr", hdr(dw=((-8, -8), (W + 7, H + 7)), disp=((0, 0), (W - 1, H - 1))),
      {"R": r, "G": r.copy(), "B": r.copy()})

# 7. multipart: part 0 "AO" (R=0.9), part 1 "beauty" (R=0.5). Expect beauty.
p0 = OpenEXR.Part(hdr(), {"R": solid(0.9), "G": solid(0.9), "B": solid(0.9)}, name="AO")
p1 = OpenEXR.Part(hdr(), {"R": solid(0.5), "G": solid(0.25), "B": solid(0.125)}, name="beauty")
OpenEXR.File([p0, p1]).write("multipart_beauty.exr")

# 8. multipart: part 0 has only Z, part 1 has RGB (R=0.5). Expect part 1's RGB.
p0 = OpenEXR.Part(hdr(), {"Z": solid(3.0)}, name="depth")
p1 = OpenEXR.Part(hdr(), {"R": solid(0.5), "G": solid(0.25), "B": solid(0.125)}, name="rgba")
OpenEXR.File([p0, p1]).write("multipart_rgb_second.exr")

# 9. matcap policy unchanged: diffuse + specular summed. Expect R = 0.25 + 0.5 = 0.75.
write("diffuse_specular.exr", hdr(comp=OpenEXR.DWAA_COMPRESSION), {
    "diffuse.R": solid(0.25), "diffuse.G": solid(0.25), "diffuse.B": solid(0.25),
    "specular.R": solid(0.5), "specular.G": solid(0.5), "specular.B": solid(0.5)})

# 10. plain RGBA default layer (regression for the ordinary case). Expect R=0.5, A=0.75.
write("plain_rgba.exr", hdr(), {"R": solid(0.5), "G": solid(0.25), "B": solid(0.125),
                                "A": solid(0.75)})

# 11. large file for the leak test: 2048x2048 RGBA, then truncated -> throws mid-read.
big = np.random.default_rng(1).random((2048, 2048), dtype=np.float32)
write("big.exr", hdr(), {"R": big, "G": big, "B": big, "A": big})
data = open("big.exr", "rb").read()
open("big_truncated.exr", "wb").write(data[: len(data) // 2])
# SVG sizing cases (#11): wide / tall / ordinary / absurd intrinsic size.
SVG = ('<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 100 50">'
       '<rect width="100" height="50" fill="red"/></svg>')
for name, w, h in [("wide", "20000", "1000"), ("tall", "500", "20000"),
                   ("normal", "300", "150"), ("huge", "1e20", "5e19")]:
    open(name + ".svg", "w").write(SVG.format(w=w, h=h))
print("exr+svg ok")
