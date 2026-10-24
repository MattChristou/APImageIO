# archive

Retired diagnostic code, kept for reference. Nothing here is built.

- `crypto_probe.c` — the `AP_CryptoProbe` pixel filter used in July 2026 to
  prove LightWave pixel filters run per sample (`renderState` sampleIndex /
  numSamples / subPixelXY). Its findings shaped `crypto_matte.c`.
- `spike-oiio/` — the June 2026 spike that checked static OpenImageIO links and
  runs inside a DLL. Statically linked OIIO crashes before `main` in a console
  EXE but works inside a DLL/.p. CMake build output removed; regenerate
  with CMake if needed.
