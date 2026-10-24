/* DLL-load harness: proves OIIO static globals initialize correctly when OIIO
   is linked into a /MT DLL (as in our LightWave .p), unlike the EXE-startup
   TLS-ordering crash. Exports runtest(); the stub exe LoadLibrary's us. */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <OpenImageIO/imageio.h>
#include "../src/oiiobridge.h"

extern "C" __declspec(dllexport) void print_formats(void)
{
    std::string el = OIIO::get_string_attribute("extension_list");
    fprintf(stderr, "[dll] OIIO extension_list:\n%s\n", el.c_str());
    fflush(stderr);
}

extern "C" __declspec(dllexport) int runtest(const char* file)
{
    fprintf(stderr, "[dll] runtest start\n"); fflush(stderr);
    fprintf(stderr, "[dll] can_load=%d\n", oiiobridge_can_load(file)); fflush(stderr);
    OiioImage img; char err[256] = {0};
    memset(&img, 0, sizeof(img));
    int ok = oiiobridge_load(file, &img, err, sizeof(err));
    fprintf(stderr, "[dll] load=%d err=%s\n", ok, err); fflush(stderr);
    if (ok) {
        fprintf(stderr, "[dll] %dx%d alpha=%d hdr=%d\n",
                img.width, img.height, img.has_alpha, img.is_hdr);
        fflush(stderr);

        /* Roundtrip each format we want to be SURE is registered at runtime
           (static-lib readers/writers can be stripped if unreferenced). Build
           a float RGBA buffer from the loaded image and save+reload. */
        int w = img.width, h = img.height;
        float* rgba = (float*)malloc((size_t)w * h * 4 * sizeof(float));
        if (rgba) {
            for (int i = 0; i < w * h; i++) {
                if (img.rgbaf) { for (int c = 0; c < 4; c++) rgba[i*4+c] = img.rgbaf[i*4+c]; }
                else { for (int c = 0; c < 4; c++) rgba[i*4+c] = img.rgba8[i*4+c] / 255.0f; }
            }
            struct { const char* f; int nchan; int bd; } outs[] = {
                { "C:\\tmp\\matcap\\rt.tif", 4, 16 }, { "C:\\tmp\\matcap\\rt_f.tif", 4, 32 },
                { "C:\\tmp\\matcap\\rt.dpx", 3, 16 }, { "C:\\tmp\\matcap\\rt.hdr", 3, 32 },
                { "C:\\tmp\\matcap\\rt.exr", 4, 32 }, { 0, 0, 0 } };
            for (int k = 0; outs[k].f; k++) {
                char e2[256] = {0};
                int sv = oiiobridge_save(outs[k].f, rgba, w, h, outs[k].nchan, outs[k].bd, e2, sizeof(e2));
                OiioImage rb; memset(&rb, 0, sizeof(rb));
                int rl = sv ? oiiobridge_load(outs[k].f, &rb, e2, sizeof(e2)) : 0;
                fprintf(stderr, "[dll] roundtrip %-22s save=%d reload=%d %s\n",
                        outs[k].f, sv, rl, e2);
                fflush(stderr);
                if (rl) oiiobridge_free(&rb);
            }
            free(rgba);
        }
        oiiobridge_free(&img);
    }
    return ok;
}
