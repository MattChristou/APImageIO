/*
 * apimageio_module.c - APImageIO plugin entry point.
 *
 * Hosts custom LightWave image I/O servers. Phase 1 registers a single
 * OpenEXR-backed "ImageLoader" that handles EXR files the stock loader
 * rejects (DWAA/DWAB compression, multi-layer files). The module is a
 * multi-server shell so additional loaders/savers (PNG, EXR saver) can be
 * added later without new boilerplate.
 */

#include <lwserver.h>
#include <lwmodule.h>
#include <lwhost.h>
#include <lwimageio.h>
#include <lwgeneric.h>
#include <lwfilter.h>

/* Server activations. */
extern int ExrLoader(int, GlobalFunc *, void *, void *);          /* exr_loader.c */
extern int WebpLoader(int, GlobalFunc *, void *, void *);         /* webp_loader.c */
extern int WebpSaverLossless(int, GlobalFunc *, void *, void *);  /* webp_saver.c */
extern int WebpSaverLossy100(int, GlobalFunc *, void *, void *);  /* webp_saver.c */
extern int WebpSaverLossy90(int, GlobalFunc *, void *, void *);   /* webp_saver.c */
extern int WebpSaverLossy75(int, GlobalFunc *, void *, void *);   /* webp_saver.c */
extern int AvifLoader(int, GlobalFunc *, void *, void *);         /* avif_loader.c */
extern int AvifSaverLossless(int, GlobalFunc *, void *, void *);  /* avif_saver.c */
extern int AvifSaverQ90(int, GlobalFunc *, void *, void *);       /* avif_saver.c */
extern int AvifSaverQ60(int, GlobalFunc *, void *, void *);       /* avif_saver.c */
extern int SvgLoader(int, GlobalFunc *, void *, void *);          /* svg_loader.c */
extern int PngLoader(int, GlobalFunc *, void *, void *);          /* png_loader.c (16-bit only) */
extern int KraLoader(int, GlobalFunc *, void *, void *);          /* kra_loader.c (Krita .kra / OpenRaster .ora) */
extern int OiioLoader(int, GlobalFunc *, void *, void *);         /* oiio_loader.c (catch-all) */
extern int OiioSaverTiff16(int, GlobalFunc *, void *, void *);    /* oiio_saver.c */
extern int OiioSaverTiffFloat(int, GlobalFunc *, void *, void *); /* oiio_saver.c */
extern int OiioSaverDpx(int, GlobalFunc *, void *, void *);       /* oiio_saver.c */
extern int OiioSaverHdr(int, GlobalFunc *, void *, void *);       /* oiio_saver.c */
extern int OiioSaverFits(int, GlobalFunc *, void *, void *);      /* oiio_saver.c */
extern int OiioSaverIco(int, GlobalFunc *, void *, void *);       /* oiio_saver.c */
extern int OiioSaverIff(int, GlobalFunc *, void *, void *);       /* oiio_saver.c */
extern int OiioSaverPnm(int, GlobalFunc *, void *, void *);       /* oiio_saver.c */
extern int OiioSaverZfile(int, GlobalFunc *, void *, void *);     /* oiio_saver.c */
extern int OiioSaverExr(int, GlobalFunc *, void *, void *);       /* oiio_saver.c (EXR + compression) */
extern int ApExrCompressionCmd(int, GlobalFunc *, void *, void *);/* oiio_saver.c (LayoutGeneric command) */
extern int ApExrBitDepthCmd(int, GlobalFunc *, void *, void *);   /* oiio_saver.c (LayoutGeneric command) */
extern int OiioMultilayerExr(int, GlobalFunc *, void *, void *);  /* exr_multilayer.c (ImageFilter) */
extern int ApMultilayerOutputCmd(int, GlobalFunc *, void *, void *);/* exr_multilayer.c (command) */
extern int CryptoMatte(int, GlobalFunc *, void *, void *);        /* crypto_matte.c (PixelFilter accum) */
extern int CryptoMatteOutput(int, GlobalFunc *, void *, void *);  /* crypto_matte.c (ImageFilter write) */
extern int CryptomatteOutputCmd(int, GlobalFunc *, void *, void *);/* crypto_matte.c (command) */
extern int CryptomatteModeCmd(int, GlobalFunc *, void *, void *);  /* crypto_matte.c (command) */
extern int ApLoadImageCmd(int, GlobalFunc *, void *, void *);      /* load_image_cmd.c (command) */
extern int ApPrefsPanel(int, GlobalFunc *, void *, void *);       /* ap_prefs.c (LayoutGeneric) */
extern int ApSetOutputPathCmd(int, GlobalFunc *, void *, void *); /* ap_prefs.c (menu: pick output path) */
extern int ApSetOcioCmd(int, GlobalFunc *, void *, void *);       /* ap_prefs.c (menu: pick OCIO config) */

static ServerTagInfo ExrLoaderTags[] = {
    { "OpenEXR (DWA/Multilayer)", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "EXR", SRVTAG_DESCRIPTION | LANGID_USENGLISH },
    /* Supersede LightWave's stock EXR loader so we own .exr (handles DWAA/DWAB
       + multi-layer that the stock one rejects). Class/Name syntax required. */
    { "ImageLoader/LW_OpenEXR", SRVTAG_REPLACES },
    { NULL, 0 }
};

static ServerTagInfo WebpLoaderTags[] = {
    { "WebP", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "WebP", SRVTAG_DESCRIPTION | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo WebpSaverLosslessTags[] = {
    { "WebP Lossless", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo WebpSaverLossy100Tags[] = {
    { "WebP Lossy 100", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo WebpSaverLossy90Tags[] = {
    { "WebP Lossy 90", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo WebpSaverLossy75Tags[] = {
    { "WebP Lossy 75", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo AvifLoaderTags[] = {
    { "AVIF", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "AVIF", SRVTAG_DESCRIPTION | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo AvifSaverLosslessTags[] = {
    { "AVIF Lossless", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo AvifSaverQ90Tags[] = {
    { "AVIF q90", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo AvifSaverQ60Tags[] = {
    { "AVIF q60", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo SvgLoaderTags[] = {
    { "SVG", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "SVG", SRVTAG_DESCRIPTION | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo OiioLoaderTags[] = {
    { "OpenImageIO (TIFF/DPX/PSD/HDR/JP2/RAW/FITS/ICO/IFF/PNM/...)", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "OIIO", SRVTAG_DESCRIPTION | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo OiioSaverFitsTags[]  = { { "OIIO FITS (float)", SRVTAG_USERNAME | LANGID_USENGLISH }, { NULL, 0 } };
static ServerTagInfo OiioSaverIcoTags[]   = { { "OIIO ICO", SRVTAG_USERNAME | LANGID_USENGLISH }, { NULL, 0 } };
static ServerTagInfo OiioSaverIffTags[]   = { { "OIIO IFF (Maya)", SRVTAG_USERNAME | LANGID_USENGLISH }, { NULL, 0 } };
static ServerTagInfo OiioSaverPnmTags[]   = { { "OIIO PNM/PPM", SRVTAG_USERNAME | LANGID_USENGLISH }, { NULL, 0 } };
static ServerTagInfo OiioSaverZfileTags[] = { { "OIIO Zfile", SRVTAG_USERNAME | LANGID_USENGLISH }, { NULL, 0 } };

static ServerTagInfo OiioSaverTiff16Tags[] = {
    { "OIIO TIFF 16-bit", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo OiioSaverTiffFloatTags[] = {
    { "OIIO TIFF float", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo OiioSaverDpxTags[] = {
    { "OIIO DPX", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo OiioSaverHdrTags[] = {
    { "OIIO HDR (Radiance)", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo PngLoaderTags[] = {
    { "PNG 16-bit", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "PNG", SRVTAG_DESCRIPTION | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo KraLoaderTags[] = {
    { "Krita (.kra) / OpenRaster (.ora)", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "KRA", SRVTAG_DESCRIPTION | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo OiioSaverExrTags[] = {
    { "OpenEXR (APImageIO)", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "EXR", SRVTAG_DESCRIPTION | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo ApExrCompressionTags[] = {
    { "APImageIO EXR Compression", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo ApExrBitDepthTags[] = {
    { "APImageIO EXR Bit Depth", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo OiioMultilayerExrTags[] = {
    { "Multilayer OpenEXR (APImageIO)", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo ApMultilayerOutputTags[] = {
    { "APImageIO Multilayer EXR Output", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo CryptoMatteTags[] = {
    { "AP Cryptomatte", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo CryptoMatteOutputTags[] = {
    { "AP Cryptomatte Output", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo CryptomatteOutputCmdTags[] = {
    { "APImageIO Cryptomatte Output", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo CryptomatteModeCmdTags[] = {
    { "APImageIO Cryptomatte Mode", SRVTAG_USERNAME | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo ApPrefsPanelTags[] = {
    { "APImageIO Settings", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "Utilities/APImageIO Settings...", SRVTAG_MENU | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo ApSetOutputPathTags[] = {
    { "APImageIO Set Output Path", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "Utilities/APImageIO Set Output Path...", SRVTAG_MENU | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerTagInfo ApSetOcioTags[] = {
    { "APImageIO Set OCIO Config", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "Utilities/APImageIO Set OCIO Config...", SRVTAG_MENU | LANGID_USENGLISH },
    { NULL, 0 }
};

/* "APImageIO_LoadImage <path>" for scripts; the menu entry (no path) opens a
   multi-select requester. LightWave itself has no LoadImage command. */
static ServerTagInfo ApLoadImageTags[] = {
    { "APImageIO Load Image", SRVTAG_USERNAME | LANGID_USENGLISH },
    { "Utilities/APImageIO Load Images...", SRVTAG_MENU | LANGID_USENGLISH },
    { NULL, 0 }
};

static ServerRecord ServerDesc[] = {
    { LWIMAGELOADER_CLASS, "AP_OpenEXRLoader",          (ActivateFunc *)ExrLoader,         ExrLoaderTags },
    { LWIMAGELOADER_CLASS, "AP_WebP",                   (ActivateFunc *)WebpLoader,        WebpLoaderTags },
    { LWIMAGESAVER_CLASS,  "AP_WebP_lossless(.webp)",   (ActivateFunc *)WebpSaverLossless, WebpSaverLosslessTags },
    { LWIMAGESAVER_CLASS,  "AP_WebP_q100(.webp)",       (ActivateFunc *)WebpSaverLossy100, WebpSaverLossy100Tags },
    { LWIMAGESAVER_CLASS,  "AP_WebP_q90(.webp)",        (ActivateFunc *)WebpSaverLossy90,  WebpSaverLossy90Tags },
    { LWIMAGESAVER_CLASS,  "AP_WebP_q75(.webp)",        (ActivateFunc *)WebpSaverLossy75,  WebpSaverLossy75Tags },
    { LWIMAGELOADER_CLASS, "AP_AVIF",                   (ActivateFunc *)AvifLoader,        AvifLoaderTags },
    { LWIMAGESAVER_CLASS,  "AP_AVIF_lossless(.avif)",   (ActivateFunc *)AvifSaverLossless, AvifSaverLosslessTags },
    { LWIMAGESAVER_CLASS,  "AP_AVIF_q90(.avif)",        (ActivateFunc *)AvifSaverQ90,      AvifSaverQ90Tags },
    { LWIMAGESAVER_CLASS,  "AP_AVIF_q60(.avif)",        (ActivateFunc *)AvifSaverQ60,      AvifSaverQ60Tags },
    { LWIMAGELOADER_CLASS, "AP_SVG",                    (ActivateFunc *)SvgLoader,         SvgLoaderTags },
    { LWIMAGELOADER_CLASS, "AP_PNG16",                  (ActivateFunc *)PngLoader,         PngLoaderTags },
    { LWIMAGELOADER_CLASS, "AP_KRA",                    (ActivateFunc *)KraLoader,         KraLoaderTags },
    { LWIMAGELOADER_CLASS, "AP_OIIO",                   (ActivateFunc *)OiioLoader,        OiioLoaderTags },
    { LWIMAGESAVER_CLASS,  "AP_TIFF_16bit(.tif)",       (ActivateFunc *)OiioSaverTiff16,   OiioSaverTiff16Tags },
    { LWIMAGESAVER_CLASS,  "AP_TIFF_float(.tif)",       (ActivateFunc *)OiioSaverTiffFloat,OiioSaverTiffFloatTags },
    { LWIMAGESAVER_CLASS,  "AP_DPX(.dpx)",              (ActivateFunc *)OiioSaverDpx,      OiioSaverDpxTags },
    { LWIMAGESAVER_CLASS,  "AP_HDR(.hdr)",              (ActivateFunc *)OiioSaverHdr,      OiioSaverHdrTags },
    { LWIMAGESAVER_CLASS,  "AP_FITS(.fits)",            (ActivateFunc *)OiioSaverFits,     OiioSaverFitsTags },
    { LWIMAGESAVER_CLASS,  "AP_ICO(.ico)",              (ActivateFunc *)OiioSaverIco,      OiioSaverIcoTags },
    { LWIMAGESAVER_CLASS,  "AP_IFF(.iff)",              (ActivateFunc *)OiioSaverIff,      OiioSaverIffTags },
    { LWIMAGESAVER_CLASS,  "AP_PNM(.ppm)",              (ActivateFunc *)OiioSaverPnm,      OiioSaverPnmTags },
    { LWIMAGESAVER_CLASS,  "AP_Zfile(.zfile)",          (ActivateFunc *)OiioSaverZfile,    OiioSaverZfileTags },
    { LWIMAGESAVER_CLASS,  "AP_OpenEXR(.exr)",          (ActivateFunc *)OiioSaverExr,      OiioSaverExrTags },
    { LWLAYOUTGENERIC_CLASS, "APImageIO_EXRCompression",(ActivateFunc *)ApExrCompressionCmd, ApExrCompressionTags },
    { LWLAYOUTGENERIC_CLASS, "APImageIO_EXRBitDepth",   (ActivateFunc *)ApExrBitDepthCmd,   ApExrBitDepthTags },
    { LWIMAGEFILTER_HCLASS,  "AP_MultilayerEXR",        (ActivateFunc *)OiioMultilayerExr,  OiioMultilayerExrTags },
    { LWLAYOUTGENERIC_CLASS, "APImageIO_MultilayerOutput",(ActivateFunc *)ApMultilayerOutputCmd, ApMultilayerOutputTags },
    { LWPIXELFILTER_HCLASS,  "AP_Cryptomatte",          (ActivateFunc *)CryptoMatte,       CryptoMatteTags },
    { LWIMAGEFILTER_HCLASS,  "AP_CryptomatteOutput",    (ActivateFunc *)CryptoMatteOutput, CryptoMatteOutputTags },
    { LWLAYOUTGENERIC_CLASS, "APImageIO_CryptomatteOutput",(ActivateFunc *)CryptomatteOutputCmd, CryptomatteOutputCmdTags },
    { LWLAYOUTGENERIC_CLASS, "APImageIO_CryptomatteMode",(ActivateFunc *)CryptomatteModeCmd, CryptomatteModeCmdTags },
    { LWLAYOUTGENERIC_CLASS, "AP_ImageIO_Settings",     (ActivateFunc *)ApPrefsPanel,      ApPrefsPanelTags },
    { LWLAYOUTGENERIC_CLASS, "AP_ImageIO_SetOutputPath",(ActivateFunc *)ApSetOutputPathCmd, ApSetOutputPathTags },
    { LWLAYOUTGENERIC_CLASS, "AP_ImageIO_SetOcio",      (ActivateFunc *)ApSetOcioCmd,      ApSetOcioTags },
    { LWLAYOUTGENERIC_CLASS, "APImageIO_LoadImage",     (ActivateFunc *)ApLoadImageCmd,    ApLoadImageTags },
    { NULL }
};

void *Startup(void)
{
    return (void *)1;
}

void Shutdown(void *serverData)
{
    (void)serverData;
}

__declspec(dllexport) ModuleDescriptor _mod_descrip = {
    MOD_SYSSYNC,
    MOD_SYSVER,
    MOD_MACHINE,
    Startup,
    Shutdown,
    ServerDesc
};
