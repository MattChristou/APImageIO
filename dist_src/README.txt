APIMAGEIO for LightWave 3D  -  version 0.4.0-beta.1
====================================================

Thanks for testing! APImageIO teaches LightWave to read and write image
formats it doesn't handle on its own, and adds multilayer EXR output and
Cryptomatte ID mattes to your renders:

  - OpenEXR files LightWave can't open: DWAA/DWAB compression, multilayer
    files from Blender, Nuke and Houdini, cropped or overscan EXRs.
  - WebP, AVIF, SVG, JPEG XL, 16-bit PNG, Krita (.kra) and OpenRaster (.ora).
  - TIFF, DPX, Cineon, PSD, Radiance HDR, JPEG 2000, DDS, camera RAW and more,
    through OpenImageIO.
  - Savers for WebP, AVIF, OpenEXR, TIFF 16-bit/float, DPX, HDR and others.
  - One multilayer EXR per frame holding all your render buffers.
  - Cryptomatte for clean object/material/asset mattes in Nuke, Fusion,
    Resolve, After Effects and other compositors.

This is a BETA. Please don't rely on it for production work you can't afford
to redo, and please tell us about anything odd (see "Reporting problems").


REQUIREMENTS
------------
- LightWave 3D 2025, Windows 64-bit. Nothing else to install.
- LightWave 2026 is untested; reports are welcome.


INSTALLING
----------
1. Copy  Plugins\APImageIO.p  to any folder you keep plug-ins in.
2. In Layout: Utilities tab > Plugins > Add Plugins..., and pick APImageIO.p.
3. Restart Layout once so the image loaders are in place for every file
   requester.

To update to a newer beta: quit LightWave, replace APImageIO.p, restart.

To check which build you have: run  APImageIO Settings...  from the
Utilities tab (Additional menu). The version appears on the message line at
the bottom of Layout and in the "APImageIO Version" row at the bottom of the
APImageIO preferences page - please include it in reports.


QUICK CHECK WITH THE SAMPLES
----------------------------
The Samples folder has one image per loader. Each image says what it is and
what you should see. Load them in the Image Editor (or as a texture):

  EXR_Blender_Multilayer_DWAA.exr  green card, "Combined layer - CORRECT".
                                   A red "WRONG LAYER" card means the wrong
                                   layer was picked.
  EXR_BBox_Crop_1920x1080.exr      should be 1920 x 1080, a blue box centred
                                   on black.
  EXR_Luminance_Y.exr              a grey card (the file has no RGB at all).
  PNG16_Smooth_Gradient.png        a very gentle grey ramp with NO visible
                                   steps (8-bit loading shows bands).
  WebP_Sample.webp                 purple card.
  AVIF_Alpha_Sample.avif           orange card with transparent corners.
  JXL_Sample.jxl, DPX_Sample.dpx   loaded through OpenImageIO.
  SVG_Wide_4to1.svg                2048 x 512, both circles round.
  Krita_Sample.kra,
  OpenRaster_Sample.ora            "Full-size composite - CORRECT" at
                                   1024 x 512. A small black "THUMBNAIL"
                                   image means the wrong part was read.


LOADING IMAGES
--------------
Nothing to switch on: load images as usual. APImageIO only takes the files
it is meant for and leaves everything else to LightWave's own loaders.

OpenEXR
    Replaces LightWave's EXR loader. For multilayer files it picks, in order:
    the default RGB(A) layer; a "diffuse" layer with "specular" added (the
    Blender matcap convention); a layer named like a beauty pass (Combined,
    beauty, rgba, ...); otherwise the first RGB layer. Files with no RGB load
    as grey (luminance, or a single channel such as Z). Cropped and overscan
    files load at their format size.
16-bit PNG
    Only 16-bit PNGs are taken (LightWave's own loader reduces them to 8-bit);
    ordinary PNGs are untouched.
Krita / OpenRaster
    Reads the flattened image Krita stores in the file, so you can texture
    straight from your working file. Re-load the image after saving in Krita.
SVG
    Rasterised once at the document's own size (largest side up to 8192
    pixels). It doesn't re-render when you zoom.
OpenImageIO formats
    .tif .tiff .dpx .cin .psd .hdr .jp2 .j2k .jxl .dds .sgi .rla .pic .tx
    .fits .ico .pnm/.ppm/.pgm .zfile .iff (Maya IFF) and camera RAW (.cr2
    .cr3 .nef .arw .dng .raf .orf .rw2 and more).

COLOUR SPACE: 16-bit PNG, high-bit-depth AVIF and some OpenImageIO files
arrive as floating point. If one looks too bright or washed out, set its
colour space to sRGB in the Image Editor.

If a file can't be read, the reason appears on Layout's message line (for
example "OpenEXR: can't load shot.exr - ...").

Loading from the menu or a script
    Utilities > APImageIO Load Images... opens a file requester where you
    can pick several images at once; they are added to the Image Editor.
    LightWave has no command for loading an image file, so APImageIO adds
    one for macros, LScript and Python:

        APImageIO_LoadImage C:\Textures\wall.exr

    The path is everything after the command name - no quotes needed (quotes
    are accepted). A file that is already loaded is not loaded twice. The
    result is shown on the message line. Without a path the command opens
    the file requester, so always give a path in unattended scripts.


SAVING IMAGES
-------------
Pick the format in Render Properties > Output as the image type:

  WebP Lossless / Lossy 100 / 90 / 75
  AVIF Lossless / q90 / q60
  OpenEXR (APImageIO)       compression and half/full float come from
                            Preferences (see below)
  OIIO TIFF 16-bit, OIIO TIFF float, OIIO DPX, OIIO HDR (Radiance),
  OIIO FITS, OIIO IFF (Maya), OIIO PNM/PPM, OIIO ICO, OIIO Zfile


MULTILAYER EXR (all render buffers in one file)
-----------------------------------------------
1. Choose an output folder: "Multilayer/Cryptomatte Output Path" in the
   APImageIO preferences (or Utilities > APImageIO Set Output Path...).
   Files are named after the scene: MyScene_0001.exr, MyScene_0002.exr, ...
2. Effects panel > Processing > Add Image Filter > Multilayer OpenEXR
   (APImageIO).
3. Render. Every buffer the render uses is written, named
   <Buffer>.R/G/B/A (or .Z for single-channel buffers).

With "EXR: 16-bit Half Float" on, colour buffers are written half; data
buffers (Depth, IDs, positions, normals, motion, UVs) always stay 32-bit
float so their values are exact.


CRYPTOMATTE
-----------
See Cryptomatte.md for the full guide. In short:
1. Effects panel > Processing > Add Pixel Filter > AP Cryptomatte.
2. Choose the "Multilayer/Cryptomatte Output Path" folder, then either
   add the image filter  AP Cryptomatte Output  (writes
   MyScene_0001.cryptomatte.exr there), or set the "Cryptomatte Output"
   preference to "Merge into Multilayer EXR" and use the
   Multilayer filter above.
3. Render with antialiasing on, open the EXR in your compositor, and pick
   with its Cryptomatte node: CryptoObject, CryptoMaterial and CryptoAsset
   (a whole parented hierarchy as one matte).


PREFERENCES
-----------
Run  APImageIO Settings...  (Utilities tab, Additional menu) to open the
APImageIO page of LightWave's Preferences.
ImageCache Memory (MB), Worker Threads (0 = automatic), Auto-Mipmap Textures
    OpenImageIO engine settings.
RAW: Use Camera White Balance / Auto-Brighten / Highlight Mode
    How camera RAW files are developed.
EXR: Default Compression, EXR: 16-bit Half Float
    For the EXR saver and the multilayer / Cryptomatte output. Files that
    contain Cryptomatte always use a lossless compression (ZIP is used
    instead of DWAA/DWAB, PXR24 or B44).
Cryptomatte Output
    Own EXR, or merged into the Multilayer EXR.
Multilayer/Cryptomatte Output Path
    The folder those files are written to, named after the scene.
Enable Texture Streaming, Default Input Colour Space, Default OCIO Config
    Reserved for a future update - they currently have no effect.


KNOWN LIMITATIONS
-----------------
- Folder or file names with non-English characters (e.g. "Ålesund") have not
  been tested. Please tell us if loading or saving fails in such a folder.
- The OIIO ICO saver can only write images up to 256 x 256, and the OIIO
  Zfile saver may refuse colour images. Both are unconfirmed - reports
  welcome.
- In the multilayer EXR, a lossy compression (DWAA/DWAB, PXR24) also applies
  to data buffers such as depth and positions. Use ZIP or PIZ if you need
  those exact.
- Cryptomatte has been checked in Affinity Photo; we'd especially like to
  hear how picking works in Nuke, Fusion, Resolve and After Effects.
- .iff is shared: LightWave's own loader reads Amiga IFF, and Maya IFF is
  read through OpenImageIO.


REPORTING PROBLEMS
------------------
Please include:
- the APImageIO version (see Installing) and your LightWave version;
- any text shown on Layout's message line;
- what you expected and what happened, and the steps to reproduce it;
- if you can, the image file (or the scene) that shows the problem;
- for Cryptomatte: which compositor and version.

Send reports to: matthew.christou@gmail.com
