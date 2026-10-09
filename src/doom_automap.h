#ifndef DOOM_AUTOMAP_H
#define DOOM_AUTOMAP_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Lines selected by the original automap, before its integer projection.
 * Capturing native decisions preserves discovery, secret walls and IDDT. */
enum { DOOM_MAP_LINES = 2048 };
typedef struct DoomMapLine {
    double x1,y1,x2,y2;
    uint8_t color;
} DoomMapLine;
typedef struct DoomMapPose { double x,y,angle,zoom; } DoomMapPose;
typedef struct DoomMapFrame {
    DoomMapLine lines[DOOM_MAP_LINES];
    unsigned count,field;
    uint8_t text[216*144];
    DoomMapPose pose;
    uint32_t level;
    bool valid;
} DoomMapFrame;

static inline DoomMapPose DoomMapInterpolate(DoomMapPose a,DoomMapPose b,double t) {
    double da=remainder(b.angle-a.angle,65536.0);
    return (DoomMapPose){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,
        a.angle+da*t,a.zoom+(b.zoom-a.zoom)*t};
}
static inline void DoomMapProject(DoomMapPose pose,double x,double y,
    unsigned width,unsigned scale,double *sx,double *sy) {
    double angle=pose.angle*(2*3.14159265358979323846/65536);
    double dx=x-pose.x,dy=y-pose.y;
    *sx=(width*0.5+(dx*sin(angle)-dy*cos(angle))*pose.zoom)*scale;
    *sy=(23+72-(dx*cos(angle)+dy*sin(angle))*pose.zoom)*scale;
}
static inline uint32_t DoomMapBlend(uint32_t dst,uint32_t src,double alpha) {
    unsigned a=(unsigned)fmax(0,fmin(256,alpha*256));
    unsigned r=(((dst>>16)&255)*(256-a)+((src>>16)&255)*a+128)>>8;
    unsigned g=(((dst>>8)&255)*(256-a)+((src>>8)&255)*a+128)>>8;
    unsigned b=((dst&255)*(256-a)+(src&255)*a+128)>>8;
    return 0xff000000u|(r<<16)|(g<<8)|b;
}
/* Liang-Barsky clipping bounds work even for vertices far outside the map. */
static inline bool DoomMapClip(double *x1,double *y1,double *x2,double *y2,
    double left,double top,double right,double bottom) {
    double dx=*x2-*x1,dy=*y2-*y1,lo=0,hi=1;
    double p[4]={-dx,dx,-dy,dy};
    double q[4]={*x1-left,right-*x1,*y1-top,bottom-*y1};
    for(unsigned i=0;i<4;i++) {
        if(fabs(p[i])<1e-12) {if(q[i]<0)return false;continue;}
        double t=q[i]/p[i];
        if(p[i]<0)lo=fmax(lo,t);else hi=fmin(hi,t);
        if(lo>hi)return false;
    }
    *x2=*x1+hi*dx;*y2=*y1+hi*dy;
    *x1+=lo*dx;*y1+=lo*dy;
    return true;
}
static inline void DoomMapRasterLine(uint8_t *dst,size_t pitch,unsigned width,
    unsigned scale,double x1,double y1,double x2,double y2,uint8_t color,
    const uint32_t palettes[144][256],const bool visible[144]) {
    int left=20*scale,right=(width-20)*scale-1,top=23*scale,bottom=167*scale-1;
    if(!DoomMapClip(&x1,&y1,&x2,&y2,left,top,right,bottom))return;
    bool steep=fabs(y2-y1)>fabs(x2-x1);
    if(steep) {double tmp=x1;x1=y1;y1=tmp;tmp=x2;x2=y2;y2=tmp;}
    if(x1>x2) {double tmp=x1;x1=x2;x2=tmp;tmp=y1;y1=y2;y2=tmp;}
    double step=x2-x1>1e-12 ? (y2-y1)/(x2-x1) : 0;
    /* A half-native-pixel stroke stays crisp at 2x through 4x. */
    double radius=fmax(0.5,scale*0.25);
    for(int along=(int)floor(x1);along<=(int)ceil(x2);along++) {
        double across=y1+fmax(0,fmin(x2-x1,along+0.5-x1))*step;
        for(int cross=(int)floor(across-radius);cross<=(int)ceil(across+radius);cross++) {
            int x=steep ? cross : along,y=steep ? along : cross;
            if(x<left || x>right || y<top || y>bottom)continue;
            unsigned ny=y/scale-23;
            if(!visible[ny])continue;
            double coverage=fmax(0,fmin(1,radius+0.5-fabs(cross+0.5-across)));
            uint32_t *pixel=&((uint32_t*)(dst+(size_t)y*pitch))[x];
            *pixel=DoomMapBlend(*pixel,palettes[ny][color],coverage);
        }
    }
}
static inline void DoomMapRaster(const DoomMapFrame *frame,DoomMapPose pose,
    uint8_t *dst,size_t pitch,unsigned width,unsigned scale,bool overlay,
    const uint32_t palettes[144][256],const bool visible[144]) {
    for(unsigned y=0;y<144*scale;y++) {
        unsigned ny=y/scale;if(!visible[ny])continue;
        uint32_t *row=(uint32_t*)(dst+(size_t)(y+23*scale)*pitch);
        for(unsigned x=20*scale;x<(width-20)*scale;x++)
            row[x]=overlay ? DoomMapBlend(row[x],0xff000000,0.25) : palettes[ny][0x81];
    }
    for(unsigned i=0;i<frame->count;i++) {
        const DoomMapLine *line=&frame->lines[i];double x1,y1,x2,y2;
        DoomMapProject(pose,line->x1,line->y1,width,scale,&x1,&y1);
        DoomMapProject(pose,line->x2,line->y2,width,scale,&x2,&y2);
        DoomMapRasterLine(dst,pitch,width,scale,x1,y1,x2,y2,line->color,palettes,visible);
    }
    unsigned left=(width-216)/2;
    for(unsigned y=0;y<144;y++)if(visible[y])for(unsigned x=0;x<216;x++) {
        unsigned c=frame->text[y*216+x];if(!c)continue;
        for(unsigned dy=0;dy<scale;dy++) {
            uint32_t *row=(uint32_t*)(dst+(size_t)((y+23)*scale+dy)*pitch);
            for(unsigned dx=0;dx<scale;dx++)row[(left+x)*scale+dx]=palettes[y][c];
        }
    }
}
#endif
