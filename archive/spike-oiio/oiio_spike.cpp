#include <cstdio>
#include <cstring>
#include "../src/oiiobridge.h"
int main(int argc, char** argv){
    fprintf(stderr,"START\n"); fflush(stderr);
    if(argc<2){printf("usage\n");return 2;}
    fprintf(stderr,"can_load=%d\n", oiiobridge_can_load(argv[1])); fflush(stderr);
    OiioImage img; char err[256]={0}; memset(&img,0,sizeof(img));
    fprintf(stderr,"calling load...\n"); fflush(stderr);
    int ok = oiiobridge_load(argv[1],&img,err,sizeof(err));
    fprintf(stderr,"load returned %d (%s)\n", ok, err); fflush(stderr);
    if(ok){ fprintf(stderr,"OK %dx%d alpha=%d hdr=%d\n", img.width,img.height,img.has_alpha,img.is_hdr); oiiobridge_free(&img); }
    return 0;
}
