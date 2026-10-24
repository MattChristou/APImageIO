# Changelog

All notable changes to APImageIO are recorded here.

## [Unreleased]

## [0.4.0-beta.1] - 2026-10-12

First beta-tester package (`make_dist.ps1` → `dist\APImageIO_0.4.0-beta.1.zip`).

### Added
- **`APImageIO_LoadImage <path>` command** (`src/load_image_cmd.c`). LightWave
  has no command that loads an image file (`IE_CreateImage` only makes a blank
  one), so scripts, macros, the Render Pass Manager and importers had to call
  `LWImageList->load` from code. The command wraps it: the path is the rest of
  the line (unquoted, like `LoadObject`; quotes tolerated), a file already in
  the image list is not loaded again, and the result goes to the message
  line. Run from **Utilities → APImageIO Load Images…** with no path it opens a
  multi-select file requester. Loading uses LightWave's loader chain, so
  APImageIO's own loaders apply. If a loader rejects the file, its specific
  reason is left on the message line (new `ap_report_count()` lets the
  command tell). Verified in LightWave.
- **Version string** (`src/apimageio_version.h`), shown on Layout's message line
  by *APImageIO Settings…* and in a greyed-out **APImageIO Version** row at the
  bottom of the Preferences page (value rewritten every session so it always
  shows the running build), so testers can quote the build. Verified in
  LightWave.
- **Regression harness** in `tests/` (`run_tests.bat`: 48 checks over the KRA,
  EXR, SVG, Cryptomatte-key and output-path code) and self-describing sample images
  (`tools/mk_samples.py` → `dist_src/Samples`).

### Changed
- **Path settings are file fields** with a browse button, like Layout > Paths:
  Multilayer/Cryptomatte Output Path (save) and Default OCIO Config (open). The
  Utilities commands still work.
- Cryptomatte Output tooltip: an internal ';' ended it early (hints split at
  ';') - reworded.
- **Preferences page heals itself.** LightWave reloads a page's saved
  definitions at startup and a later publish does not replace them, so changed
  hints (a new control, a fixed tooltip) never showed and dropped keys lingered.
  The page is now published afresh once when a saved key's hints differ from the
  code's or a key is no longer published; values are kept. (shared apprefs)
- **Multilayer EXR: per-channel precision.** With "16-bit Half Float" on,
  only colour buffers are written half; data buffers (Depth, Object/Surface ID,
  World/Object/Texture Position, Normals, Motion, UV and tangent spaces, dPdu/
  dPdv, Geometry, Sampling Weight, Special) stay 32-bit float. Half rounded IDs
  above 2048, turned background depth beyond 65504 into infinity and lost
  position precision. Merge mode no longer forces the whole file to 32-bit:
  only the data and Crypto channels are float. New bridge entry
  `oiiobridge_save_exr_layers` (per-channel formats via `ImageOutput`).

### Fixed
- **Output path is now a folder.** "Multilayer/Cryptomatte Output Path" and
  *Set Output Path…* opened a Save requester asking for a file name, though
  the value is used as a destination. Both now pick a folder, and files are
  named after the scene: `<folder>\<Scene>_NNNN.exr` and
  `<Scene>_NNNN.cryptomatte.exr` (`ap_exr_resolve_base`, 8 harness cases). A
  value that is not a folder is still treated as a filename prefix, so
  existing settings and Render Pass Manager paths keep working. Verified in
  LightWave.
- **Cryptomatte own-EXR output ignored the output-path preference.** It only
  honoured the `APImageIO_CryptomatteOutput` command (which can't be typed by
  hand), so files always went to `%TEMP%`. It now uses "Multilayer/Cryptomatte
  Output Path" when no command override is set. Verified in LightWave.

Found by a code-review pass; offline regressions in a standalone harness (24
checks) unless noted.
- **Cryptomatte: stale names on the first render after a scene change.** The
  surface table was built when the filter was added and only cleared after a
  render, so objects/surfaces added or renamed since (or a different scene
  loaded) rendered as background or picked to old names. The table is now
  built on the first sample of every frame and freed when the frame is written.
  *(Needs an in-LightWave check.)*
- **CryptoAsset ignored the hierarchy for loaded objects.** The parent lookup
  matched the item's short name against `sceneObject()`, which returns the file
  path for a loaded `.lwo`, so it never matched and CryptoAsset silently equalled
  CryptoObject. Objects are now matched by file path or name. *(Needs an
  in-LightWave check.)*
- **Cryptomatte IDs corrupted by lossy compression.** PXR24, DWAA/DWAB and
  B44/B44A alter the ID bit patterns, so picks stopped matching the manifest.
  Any file carrying Crypto channels (own or merge mode) now falls back to ZIP
  when a lossy codec is selected.
- **Cryptomatte metadata key** is now the first 7 hex digits of the type-name
  hash, as in the spec and reference implementations (was the last 7). Nuke
  read either; tools that recompute the key now find it.
- **Multilayer EXR and Cryptomatte writes no longer fail silently.** A failed
  write (missing folder, read-only path, out of memory) now reports the reason
  on Layout's message line instead of rendering a whole sequence to nothing.
- **AVIF/WebP savers detect failed writes.** A short write or a failed
  `fclose` (full disk, dropped network share) is reported as a failure, the
  partial file is deleted, and the reason is shown on the message line. AVIF
  previously reported a truncated file as saved.
- **EXR loader: memory leak on corrupt files.** The decoded image buffer leaked
  whenever OpenEXR threw mid-read (≈64 MB per failed 2K frame). Harness: 40
  failed loads of a truncated 2048² file, memory flat.
- **EXR loader: files with no RGB layer now load.** Since APImageIO replaces
  the stock EXR loader, these previously had no loader at all. Luminance
  (`Y`, `Y`+`A`, `Y/RY/BY`) decodes to RGB; a lone channel (e.g. `Z`) shows as
  grey; a multipart file whose first part has no RGB uses the part that does.
  Deep parts are skipped. (`Y/RY/BY` chroma is decoded by OpenEXR's
  `RgbaInputFile` but not covered by the harness.)
- **EXR loader: beauty layer preferred.** With no default layer, a layer named
  like a beauty pass (`Combined`, `beauty`, `rgba`, …) is chosen before falling
  back to alphabetical order — Blender multilayer files loaded `ViewLayer.AO`
  instead of `ViewLayer.Combined`. Multipart files prefer a part named that way.
- **EXR loader: display window honoured.** The image now arrives at its format
  size: a smaller data window (a Nuke bbox crop) is padded with black, a larger
  one (overscan) is cropped. Previously the data window's size was used.
- **SVG: aspect ratio kept when clamped.** Oversized documents are scaled down
  by one factor to fit 8192 px (20000×1000 → 8192×410, was squashed to
  8192×1000), and huge or NaN sizes no longer fall back to 1024×1024.

## [0.3.0] - 2026-10-08

### Added
- **Load errors are now reported.** When a dedicated loader recognises a file
  but cannot decode it, the reason now appears in Layout's message area, e.g.
  `Krita/OpenRaster: can't load foo.kra - no mergedimage.png inside (...)` or
  `OpenEXR: can't load x.exr - no RGB layer found`. Previously only
  LightWave's generic load failure was shown. Covers the EXR, WebP, AVIF, SVG,
  16-bit PNG and Krita loaders (shared `src/ap_report.{h,c}`). Uses
  `LWMessageFuncs->error`, which writes to the message line rather than
  opening a dialog, so unattended/network renders are never blocked. The OIIO
  catch-all stays silent by design: it returns `NOREC` so LightWave's built-in
  loaders can still take overlapping formats, and an error there would be
  misleading when they then succeed. Verified in LightWave.
- **Krita `.kra` / OpenRaster `.ora` ImageLoader (`AP_KRA`).** LightWave can now
  use a Krita *working file* directly as a texture, with no export step. Both
  formats are ZIP containers holding a flattened composite at
  `mergedimage.png`; `krabridge.c` unzips that one member with the already
  linked minizip-ng and decodes it with the existing lodepng (no new
  dependency). 8-bit documents are delivered as RGBA32/RGB24 so LightWave's
  colour-space handling matches an ordinary PNG; 16-bit documents are promoted
  to float. Krita's small `preview.png` thumbnail is deliberately never used as
  a fallback — a missing `mergedimage.png` reports an error rather than
  silently loading a 256px image. Anything that is not a `.kra`/`.ora` with a
  ZIP signature returns `NOREC`, so no other loader is disturbed. This is the
  first piece of the planned Krita live-texture bridge. Verified in LightWave
  (8/16-bit, alpha, greyscale, `.ora`, 4096², and missing-composite /
  non-ZIP rejection); pixels byte-exact against an independent PNG decoder.

- **Cryptomatte: CryptoAsset type + preview channels.** Adds a third crypto type,
  **CryptoAsset**, keyed by the top-most object ancestor (hierarchy root), so a
  parented rig/group picks as one asset. Each type now also writes a **preview**
  RGB layer (`<Type>.R/.G/.B`, coverage-weighted hash colour) for viewers.
- **Multilayer/Cryptomatte output path preference + file requesters.** A
  "Multilayer/Cryptomatte Output Path" string pref lets the `AP MultilayerEXR`
  filter (and Cryptomatte merge mode) write standalone without the Render Pass
  Manager (empty = inert; a per-render session path still overrides). Two
  Utilities menu commands — **APImageIO Set Output Path…** and **Set OCIO
  Config…** — open native LightWave file requesters and store the pick into the
  respective preference. (The unified Preferences panel can't host file-browse
  buttons — no file control type / `LWPrefTypeProxy` doesn't render — so path
  picking is via these menu commands.)
- **Cryptomatte: merge-into-multilayer output mode.** New **"Cryptomatte Output"**
  preference (*Own EXR* / *Merge into Multilayer EXR*) selects where the Crypto
  channels go. `own` (default) writes the dedicated `.cryptomatte.exr`; `merge`
  folds the `CryptoObject`/`CryptoMaterial` channels + manifest into the
  `AP MultilayerEXR` output (one EXR with AOVs *and* Crypto, forced to 32-bit
  float). A per-render override command **`APImageIO_CryptomatteMode <own|merge>`**
  is also available for automation. Shared emission via
  `cm_crypto_produce`/`cm_crypto_free` (`crypto_matte.h`); session flag
  `ap_crypto_mode` (override > pref).

### Changed
- **EXR output base: trailing `.exr` stripped.** When an output base ends in
  `.exr` (e.g. picked via a Save requester), the writers no longer double the
  extension — `cm_test.exr` → `cm_test0000.exr` instead of `cm_test.exr0000.exr`.
- **Cryptomatte: multithreaded accumulation.** Each render thread accumulates
  into its own per-thread accumulator indexed by `threadIndex`, so per-sample
  adds are now **lock-free** (the per-sample `CRITICAL_SECTION` is gone). The
  per-thread results are merged at frame end (`cm_accum_merge`, unit-tested)
  before ranking. Removes the single-threaded accumulation bottleneck.

### Internal
- Retired diagnostic code moved to `archive/` (not built): the
  `AP_CryptoProbe` pixel filter (`crypto_probe.c`) and the `spike-oiio`
  sources. The spike's 257 MB CMake build output was deleted.

## [0.2.0] - 2026-07-18

### Added
- **Cryptomatte generator** — produces spec-compliant Cryptomatte from a
  LightWave render. Two cooperating servers (`src/crypto_matte.c`):
  **AP Cryptomatte** (PixelFilter) accumulates per-SAMPLE object/material coverage
  before antialiasing; **AP Cryptomatte Output** (ImageFilter) ranks the top 6 per
  pixel at frame end and writes `<base>NNNN.cryptomatte.exr` — beauty RGBA +
  `CryptoObject00/01/02` + `CryptoMaterial00/01/02` channels + the
  `cryptomatte/<key>/{name,hash=MurmurHash3_32,conversion=uint32_to_float32,manifest}`
  header. Both types are driven by the `Surface_ID` buffer (surface name →
  material, `sceneObject` → object). Output path via command
  **`APImageIO_CryptomatteOutput <base>`**; with no path set it defaults to
  `%TEMP%\ap_cryptomatte_`. See `CRYPTOMATTE.md` for usage.
  - Coverage accumulator: `src/crypto_accum.{h,c}` — per-pixel coverage-ranked id
    maps, normalized by actual per-pixel sample count (robust to adaptive
    sampling); unit-tested standalone.
  - Encoding: `src/crypto_encode.{h,c}` — MurmurHash3 x86_32, uint32→float32 id
    (clamped exponent, no NaN/Inf), 7-hex type key, JSON manifest; unit-tested.
  - Verified in-engine: valid 24-channel EXR + correct manifests; hashing matches
    the reference (`"Sphere"` → `e5e9b0db`).
  - Current limits: single-threaded accumulation (per-sample lock); own-EXR output
    only (merge-into-multilayer planned); no preview RGB pass yet.
- **OpenEXR saver** (`AP_OpenEXR(.exr)`, `src/oiio_saver.c` + `oiiobridge_save_exr`)
  — RGBA half/float via OIIO with a settable compression attribute. Two prefs:
  **EXR: Default Compression** (ZIP/PIZ/RLE/PXR24/ZIPS/DWAA/DWAB/B44/B44A/None)
  and **EXR: 16-bit Half Float**. Command **`APImageIO_EXRCompression <name>`**
  sets a per-render session override (override > pref) read by the saver.
- **Multilayer OpenEXR image filter** (`AP_MultilayerEXR`, `src/exr_multilayer.c`)
  — reads render buffers via `LWFilterAccess::getLine` and writes ONE multilayer
  EXR per frame (`oiiobridge_save_exr_multi`, named channels). Command
  **`APImageIO_MultilayerOutput <path>`** sets the output base (frame+.exr
  appended); inert if unset. Shared EXR session state in `src/exr_session.{h,c}`.
  Multilayer filter validated in-app (Final_Render + Depth) then generalised to
  Final_Render + all `buffersInUse()` (each buffer's layers become named
  channels `<Buffer>.<R/G/B/A|Z|n>`).
- **Preferences panel** — OIIO/OCIO engine settings published into LightWave's
  unified **Preferences** panel via `LWPreferenceSystem` (new `src/ap_prefs.{h,c}`).
  Flat `/APImageIO/` category with: ImageCache Memory (MB), I/O Threads (0 = auto),
  Auto-Mipmap, Enable Texture Streaming, Default Input Colour Space, camera-RAW
  options (Use Camera White Balance / Auto-Brighten / Highlight Mode), and a
  fallback Default OCIO Config path.
- **"APImageIO Settings"** LayoutGeneric command (Utilities menu) — publishes the
  branch and opens Preferences on it, so the category is available from a cold
  start (a pure loader/saver only publishes lazily on first image I/O).

### Changed
- OIIO loader/saver now consume the preferences: thread count applies globally to
  decode and encode; ImageCache budget + auto-mip are applied to the shared
  `ImageCache`; the camera-RAW options are passed as a per-decode config to
  `oiiobridge_load`. `oiiobridge` gained an `OiioConfig` vocabulary and an
  `oiiobridge_configure()` entry point (bridge stays preferences-agnostic).

### Removed
- Reverted the in-progress "OIIO Texture" and "OCIO Colour Transform" nodes (and
  the `ociobridge`/`oiiobridge_texture` backends) added earlier this cycle: they
  **duplicated and name-collided** with the mature **APTexture** plugin, which
  already ships an `AP_OIIO_Texture` node, an OIIO Image Sampler, an OCIO Color
  Space node, and more. APTexture owns the streaming-texture / OCIO-node space;
  APImageIO stays image loaders/savers + prefs.

### Fixed
- Checkbox defaults now use numeric `value.default=1/0` instead of `true/false`,
  which LW silently ignores (the checkbox then defaults to 0). This restores
  **"RAW: Use Camera White Balance"** to its intended default of ON. Found via
  the APPrefsTools inspector. Run "AP Preferences Reset Branch /APImageIO/" once
  to apply the corrected defaults to an existing config.
- Renamed the threads parameter from "I/O Threads" to "Worker Threads": the pref
  system parses `/` in a parameter name as a path separator, so the old name
  spawned a phantom `/APImageIO/I/` sub-branch and dropped the control from the
  main page. Publish now also removes that stale branch to self-heal.

### Internal
- Preferences plumbing moved onto the shared `ap_shared/apprefs` library
  (table-driven publish/read/panel); `ap_prefs.c` now holds only the parameter
  table, the typed reader, and the colour-space name helper. No behaviour change.

### Notes
- Default Input Colour Space, the streaming toggle, and the OCIO config path are
  published and readable but not yet consumed by the still-image path — reserved
  for the planned OIIO streaming texture node and OCIO backend.

## [0.1.0] - 2026-06-28

Baseline of the shipped, in-LightWave-confirmed image I/O:

### Added
- **OpenEXR** loader (DWAA/DWAB compression + multilayer, replaces stock EXR loader).
- **WebP** loader + savers (lossless, q100/q90/q75).
- **AVIF** loader (8-bit + 10/12-bit float HDR) + savers (lossless, q90, q60).
- **SVG** loader (rasterised at document size via lunasvg).
- **PNG 16-bit** loader (intercepts 16-bit PNGs only; 8-bit falls through to built-in).
- **OpenImageIO catch-all** loader + savers for TIFF (8/16/32f), DPX, Cineon, PSD,
  HDR/RGBE, JPEG-2000, Targa, DDS, JPEG XL, camera RAW, FITS, ICO, Maya IFF, PNM,
  and Zfile — gated to formats the dedicated loaders and built-ins don't cover.

All statically linked (`/MT`, self-contained).
