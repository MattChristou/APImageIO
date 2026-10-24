# Cryptomatte for LightWave (APImageIO)

Generate [Cryptomatte](https://github.com/Psyop/Cryptomatte) ID mattes from a
LightWave render, for clean object/material isolation in Nuke, Fusion, After
Effects (with the Cryptomatte plugin), DaVinci Resolve, and other compatible
compositors.

Unlike a plain "object ID" pass, Cryptomatte stores, per pixel, the *coverage-
ranked* list of the objects/materials that contributed to it — so edges,
motion blur, depth of field, and overlapping geometry all matte cleanly with no
manual keying.

## What it produces

For each rendered frame, a dedicated EXR: `<Scene>_NNNN.cryptomatte.exr`
containing:

| Content | Channels |
|---|---|
| Beauty (for the comp node's preview / pick source) | `R G B A` |
| Object matte (by object) | `CryptoObject.{R,G,B}` + `CryptoObject00/01/02.{R,G,B,A}` |
| Material matte (by surface) | `CryptoMaterial.{R,G,B}` + `CryptoMaterial00/01/02.{R,G,B,A}` |
| Asset matte (by hierarchy root) | `CryptoAsset.{R,G,B}` + `CryptoAsset00/01/02.{R,G,B,A}` |

plus the standard header metadata (`cryptomatte/<key>/name`, `/hash`,
`/conversion`, `/manifest`) that comp apps use to resolve picks to names.

For each type: a **preview** RGB layer (`<Type>.R/.G/.B`, a hash-coloured flat
image for viewers) and **six ranks** packed into three EXR "levels" of two
(id, coverage) pairs each — enough for virtually all pixels in primary
visibility.

## How to use

1. **Add the pixel filter** (Layout → *Effects → Processing*):
   **AP Cryptomatte** — accumulates per-sample data during rendering. Always
   required.

2. **Choose an output mode** (default: own EXR) in **Preferences → APImageIO →
   "Cryptomatte Output"** (*Own EXR* or *Merge into Multilayer EXR*). Open it via
   the *Utilities → APImageIO Settings…* command. (A per-render override,
   `APImageIO_CryptomatteMode own|merge`, also exists for pipeline automation such
   as the Render Pass Manager.)

   - **own** *(default)* — add the **AP Cryptomatte Output** image filter; it
     writes a dedicated `<Scene>_NNNN.cryptomatte.exr` into the folder set in
     the "Multilayer/Cryptomatte Output Path" preference. Pipeline scripts can
     override it per render with `APImageIO_CryptomatteOutput <base>`, where
     `<base>` is a folder or a filename prefix; if neither is set it defaults
     to `%TEMP%\ap_cryptomatte_NNNN.cryptomatte.exr`.
   - **merge** — the Crypto channels are folded into the **Multilayer OpenEXR
     (APImageIO)** output instead (one EXR with your AOVs *and* the Crypto
     layers). Add that image filter and choose the output folder in the
     "Multilayer/Cryptomatte Output Path" preference (or via *Utilities →
     APImageIO Set Output Path…*). Crypto and data channels are 32-bit float;
     colour AOVs follow the "EXR: 16-bit Half Float" preference. The dedicated
     output filter is inert in merge mode.

3. **Render** with **F9** (still) or **F10** (sequence). Antialiasing should be
   on (camera samples > 1) — that is what makes the mattes edge-accurate.

4. **Composite**: open the EXR in your compositor, add a Cryptomatte node, and
   pick objects/materials. The layer selector will list `CryptoObject`,
   `CryptoMaterial` and `CryptoAsset`.

## Naming

- **CryptoObject** mattes are keyed by the **object** a surface belongs to
  (`sceneObject`). For objects loaded from disk this is the object's scene name
  as LightWave reports it.
- **CryptoMaterial** mattes are keyed by the **surface (material) name**.
  Surfaces that share a name across different objects merge into one material
  matte — this is standard Cryptomatte behaviour.
- **CryptoAsset** mattes are keyed by the **top-most object ancestor** in the
  scene hierarchy — so a parented rig/group (children under a null) picks as one
  asset. An unparented object is its own asset.

## Notes & current limitations

- **Names always match the scene being rendered.** The object/material table
  is rebuilt on the first sample of every frame, so objects added, renamed or
  re-loaded after the filter was added are picked up.
- **32-bit float** is required by the format and is always used for the
  Crypto channels. In the own-EXR file the beauty is 32-bit too; in merge
  mode colour AOVs follow the "EXR: 16-bit Half Float" preference.
- **Lossless compression is enforced.** Lossy EXR codecs (DWAA/DWAB, PXR24,
  B44/B44A) would alter the ID bits and break picking, so a file carrying
  Crypto channels falls back to ZIP when one of them is selected.
- **Transparency**: coverage behind transparent surfaces is captured only to the
  extent the renderer samples through them (a limitation shared by most
  Cryptomatte implementations).

## How it works (brief)

A LightWave pixel filter is evaluated **per sample, before antialiasing**, which
is exactly the data Cryptomatte needs and which the reconstruction-averaged
`Object_ID`/`Surface_ID` buffers cannot provide. For each sample the filter reads
the `Surface_ID`, maps it (via the surface's `getIndexID`) to the object and
material names, hashes each name (MurmurHash3 → float32, per the Cryptomatte
spec), and accumulates coverage per pixel. At frame end the per-pixel ids are
ranked by coverage, normalized, and written as the ranked Crypto channels with a
matching name→hash manifest in the header.
