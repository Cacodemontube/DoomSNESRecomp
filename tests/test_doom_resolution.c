#include "doom_resolution.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static void word(uint8_t *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
int main(void) {
    uint8_t pixels[256], runs[] = {3,10,11,12,0xfd,22};
    CHECK(DoomResolutionDecodeColumn(runs,sizeof(runs),0,6,pixels));
    CHECK(!memcmp(pixels,(uint8_t[]){10,11,12,22,22,22},6));
    CHECK(!DoomResolutionDecodeColumn(runs,4,0,6,pixels));
    CHECK(!DoomResolutionDecodeColumn((uint8_t[]){0},1,0,1,pixels));
    CHECK(!DoomResolutionDecodeColumn(runs,sizeof(runs),0,257,pixels));
    uint8_t sprite_runs[] = {5,6,0,2,7,0,0xfe,8,0,0};
    CHECK(DoomResolutionDecodeSprite(sprite_runs,sizeof(sprite_runs),0,10,pixels));
    CHECK(!memcmp(pixels,(uint8_t[]){5,6,0,0,7,8,8,0,0,0},10));
    CHECK(!DoomResolutionDecodeSprite(sprite_runs,7,0,10,pixels));
    uint8_t *rom=calloc(1,0x200000), *ram=calloc(1,0x10000);
    DoomResolutionScene *scene=calloc(1,sizeof(*scene));
    CHECK(rom && ram && scene);
    rom[0x1b0686]=128;rom[0x1b0687]=127;
    word(ram+0xd6,0x7180+62);
    word(ram+0x7180+52,0x4000);word(ram+0x7180+54,0x4004);
    word(ram+0x7180+28,0x3080);
    word(ram+0x4000,128);word(ram+0x4002,(unsigned)-96);
    word(ram+0x4004,192);word(ram+0x4006,96);
    CHECK(DoomResolutionCapture(scene,ram,0x10000,0,rom,0x200000));
    CHECK(scene->count==1 && scene->segments[0].x1==-96);
    CHECK(scene->segments[0].z1==132 && scene->segments[0].z2==196);
    CHECK(scene->segments[0].world_uv);
    /* Map endpoints must not inherit camera-dependent GSU integer rounding.
     * EMVERTEXES is biased against the RAM cache and wraps at 16 bits. */
    const unsigned vertex_base=0x3080+205*14;
    word(ram+0x8e,2);word(ram+0x84,(0x6000-vertex_base)&0xffff);ram[0x7e]=0x40;
    word(rom+0x6000,128);word(rom+0x6002,96);
    word(rom+0x6004,128);word(rom+0x6006,(unsigned)-32);
    for(unsigned view=0;view<3;view++) {
        double angle=view*DOOM_SIDE_YAW;
        word(ram+0x28,view*0x2000);
        double x,z;
        CHECK(DoomResolutionMapVertex(ram,vertex_base,rom,0x200000,&x,&z));
        CHECK(fabs(x-(128*sin(angle)-96*cos(angle)))<1e-9);
        CHECK(fabs(z-(128*cos(angle)+96*sin(angle)))<1e-9);
    }
    word(ram+0x28,0);word(ram+0x8e,0);
    /* Native vertical origins, including moving door ceilings, come from
     * BUILD world heights, never quantized screen samples. */
    word(ram+0x7180+4,1);word(ram+0x7180+32,(unsigned)-42);
    word(ram+0x7180+34,86);word(ram+0x7180+36,22);word(ram+0x7180+38,6);
    CHECK(DoomResolutionCapture(scene,ram,0x10000,42,rom,0x200000));
    CHECK(scene->segments[0].texture_origin[0]==0);
    word(ram+0x7180+4,2);
    CHECK(DoomResolutionCapture(scene,ram,0x10000,42,rom,0x200000));
    CHECK(scene->segments[0].texture_origin[0]==64);
    word(ram+0x7180+36,38);
    CHECK(DoomResolutionCapture(scene,ram,0x10000,42,rom,0x200000));
    CHECK(scene->segments[0].texture_origin[0]==80);
    CHECK(scene->segments[0].texture_origin[1]==48);
    ram[0x7180+44]=129;
    CHECK(DoomResolutionCapture(scene,ram,0x10000,42,rom,0x200000));
    CHECK(scene->segments[0].texture_origin[0]==128);
    ram[0x7180+44]=0;
    /* Follow a fixed point on a wall through translation and rotation.
     * Its texel must remain fixed across frames and visibility cameras,
     * even if native angle tables and screen samples change. */
    for(unsigned frame=0;frame<120;frame++) {
        double yaw=(frame-60.0)*0.009,eye_x=frame*0.3,eye_z=frame*0.1;
        DoomResolutionSegment wall={.world_uv=true,.offset_x=7};
        double a=-96-eye_x,b=128-eye_z,c=96-eye_x;
        wall.x1=a*cos(yaw)-b*sin(yaw);wall.z1=b*cos(yaw)+a*sin(yaw);
        wall.x2=c*cos(yaw)-b*sin(yaw);wall.z2=b*cos(yaw)+c*sin(yaw);
        double point_x=(24-eye_x)*cos(yaw)-b*sin(yaw);
        double point_z=b*cos(yaw)+(24-eye_x)*sin(yaw);
        wall.angle=(int16_t)(frame*17);wall.perpendicular=(int16_t)(frame+50);
        wall.texture_offset=(int16_t)frame;
        CHECK(fabs(DoomResolutionWallCoordinate(&wall,point_x/point_z)-127)<1e-9);
        wall.texture_w[0]=63;
        CHECK(fabs(DoomResolutionWallCoordinate(&wall,point_x/point_z)-127)<1e-9);
    }
    /* Native vertex cache depths are relative to the four-unit screen
     * plane. Recover a close flat wall identically in all three cameras;
     * omitting the offset changes side depth after reprojection. */
    for(unsigned view=0;view<3;view++) {
        double yaw=view==1 ? -DOOM_SIDE_YAW : view==2 ? DOOM_SIDE_YAW : 0;
        double common_x=view==1 ? -24 : view==2 ? 24 : 0;
        double common_z=32;
        double x=common_x*cos(yaw)-common_z*sin(yaw);
        double z=common_z*cos(yaw)+common_x*sin(yaw);
        word(ram+0x4000,(unsigned)(int)lround(z-16*sin(yaw)-4));
        word(ram+0x4002,(unsigned)(int)lround(x-16*cos(yaw)));
        word(ram+0x4004,(unsigned)(int)lround(z+16*sin(yaw)-4));
        word(ram+0x4006,(unsigned)(int)lround(x+16*cos(yaw)));
        CHECK(DoomResolutionCapture(scene,ram,0x10000,0,rom,0x200000));
        CHECK(fabs(scene->segments[0].z1-(z-16*sin(yaw)))<=0.5);
        DoomResolutionHit close_hits[DOOM_RES_SEGMENTS];
        CHECK(DoomResolutionHits(scene,x/z,z/common_z,close_hits)==1);
        CHECK(fabs(close_hits[0].depth-common_z)<1.0);
    }
    word(ram+0xd6,0x7181);
    CHECK(!DoomResolutionCapture(scene,ram,0x10000,0,rom,0x200000));
    scene->count=1;
    scene->segments[0]=(DoomResolutionSegment){
        .x1=-96,.z1=128,.x2=96,.z2=192,.flags=1|0x20|0x40,.near_sector=0,
        .floor_height=-64,.ceiling_height=64,
        .far_sector=UINT16_MAX,.texture={0x100,0},
        .texture_h={128,0},.texture_w={15,0},
        .angle=0,.perpendicular=150,.texture_offset=48 };
    word(scene->sectors+2,(unsigned)-64);word(scene->sectors+4,64);
    scene->sectors[8]=100;scene->sectors[9]=101;
    for(unsigned row=0;row<33;row++)
        for(unsigned c=0;c<256;c++)rom[0x1cde00+row*256+c]=c;
    for(unsigned u=0;u<16;u++) {
        unsigned table=0x1b0102+u*3,address=0x4000+u*0x100;
        word(rom+table,address);rom[table+2]=0x40;
        rom[address++]=64;
        for(unsigned y=0;y<64;y++)rom[address++]=1+(u*7+y)%96;
        rom[address++]=64;
        for(unsigned y=64;y<128;y++)rom[address++]=1+(u*7+y)%96;
    }
    uint32_t palette[144][256];bool visible[144];
    for(unsigned y=0;y<144;y++) {
        visible[y]=true;
        for(unsigned c=0;c<256;c++)palette[y][c]=0xff000000|c;
    }
    DoomResolutionHit hits[168];
    CHECK(DoomResolutionHits(scene,0,1,hits)==1);
    CHECK(fabs(hits[0].depth-160)<1e-9);
    unsigned changed=0;
    for(int look=-42;look<=42;look+=42) {
    scene->horizon_offset=look;
    for(unsigned scale=1;scale<=4;scale++) {
        unsigned width=256*scale,height=224*scale,stride=width+7;
        uint32_t *out=malloc(stride*height*4);CHECK(out);
        for(unsigned i=0;i<stride*height;i++)out[i]=0xa5a5a5a5;
        for(unsigned x=20*scale;x<236*scale;x++) {
            double rx=(x+0.5)/scale-128;
            DoomResolutionRasterColumn(scene,rom,0x200000,0,rx,
                rx/DOOM_FOCAL,1,x,scale,(uint8_t*)out,stride*4,palette,visible,NULL);
        }
        for(unsigned y=0;y<height;y++)for(unsigned x=0;x<stride;x++)
            if(y<23*scale || y>=167*scale || x<20*scale || x>=236*scale)
                CHECK(out[y*stride+x]==0xa5a5a5a5);
        for(unsigned y=24*scale;y<166*scale;y++)for(unsigned x=21*scale;x<235*scale;x++)
            if(out[y*stride+x]!=out[(y/scale*scale)*stride+(x/scale*scale)])changed++;
        free(out);
    }
    }
    scene->horizon_offset=0;
    CHECK(changed>500);
    /* Native texture offsets crossing a repeat must not interpolate halfway
     * through the texture. Both endpoints describe the same wrapped phase. */
    DoomResolutionSegment *seg=&scene->segments[0];
    seg->uv[0][42]=(DoomResolutionUv){.u=10,.v=2,.step=1,.y=71,.valid=true};
    seg->uv[0][43]=(DoomResolutionUv){.u=11,.v=130,.step=1,.y=71,.valid=true};
    uint32_t *out=calloc(512*448,4);CHECK(out);
    /* A sky portal at a low sector ceiling must not cut the top off a
     * taller solid wall behind it. Native UPPERCLIP, however, must hide it. */
    DoomResolutionSegment saved_wall=scene->segments[0];
    scene->count=2;
    scene->segments[0]=(DoomResolutionSegment){.x1=-96,.x2=96,.z1=64,.z2=64,
        .near_sector=0,.flags=0x40|0x80,.floor_height=-64,.ceiling_height=16};
    scene->segments[1]=saved_wall;
    scene->segments[1].z1=scene->segments[1].z2=192;
    double portal_depth[288];
    DoomResolutionRasterColumn(scene,rom,0x200000,0,0,0,1,10,2,
        (uint8_t*)out,512*4,palette,visible,portal_depth);
    CHECK(portal_depth[64]==192);
    CHECK(out[(64+46)*512+10]!=0);
    scene->segments[0].flags|=0x100;
    DoomResolutionRasterColumn(scene,rom,0x200000,0,0,0,1,10,2,
        (uint8_t*)out,512*4,palette,visible,portal_depth);
    CHECK(isinf(portal_depth[64]));
    scene->count=1;scene->segments[0]=saved_wall;
    DoomResolutionWall(scene,rom,0x200000,seg,0,(85.0-108)/DOOM_FOCAL,
        128,64,10,142,144,2,(uint8_t*)out,512*4,palette,visible,NULL);
    CHECK(out[(142+46)*512+10]==(0xff000000u|73u));
    /* Camera joins must not change the texture phase of a wall. Supply
     * deliberately different quantized native samples, then project the
     * same ray through the centre and both yawed visibility cameras. */
    scene->continuous_uv=true;
    DoomResolutionSegment native_door={.world_uv=true,.x1=0,.z1=128,
        .x2=128,.z2=128,.texture_w={63,0}};
    CHECK(fabs(DoomResolutionWallCoordinate(&native_door,0))<1e-9);
    CHECK(fabs(DoomResolutionWallCoordinate(&native_door,1)-128)<1e-9);
    DoomResolutionSegment door={.perpendicular=256};
    CHECK(fabs(DoomResolutionWallCoordinate(&door,0))<1e-9);
    /* Native RSPDistance is twice map distance. A 128-unit door spans one
     * 128-column image. Smaller textures repeat with the same density. */
    CHECK(fabs(DoomResolutionWallCoordinate(&door,1)-128)<1e-9);
    const double common_ray=108.25/DOOM_FOCAL;
    DoomResolutionSegment original=*seg;
    for(unsigned view=0;view<3;view++) {
        double yaw=view==1 ? -DOOM_SIDE_YAW : view==2 ? DOOM_SIDE_YAW : 0;
        double sx,sy;DoomProjectRay(common_ray*DOOM_FOCAL,0,yaw,&sx,&sy);
        *seg=original;seg->angle=(int16_t)(view==1 ? -0x2000 : view==2 ? 0x2000 : 0);
        for(unsigned i=0;i<108;i++)
            seg->uv[0][i]=(DoomResolutionUv){.u=(uint8_t)(view*5),
                .v=(float)(view*31),.step=1,.y=71,.valid=true};
        DoomResolutionWall(scene,rom,0x200000,seg,0,(sx-108)/DOOM_FOCAL,
            160,64,20+view,120,160,2,(uint8_t*)out,512*4,palette,visible,NULL);
    }
    for(unsigned y=120;y<160;y++) {
        CHECK(out[(y+46)*512+20]==out[(y+46)*512+21]);
        CHECK(out[(y+46)*512+20]==out[(y+46)*512+22]);
        CHECK(out[(y+46)*512+20]!=0);
    }
    *seg=original;scene->continuous_uv=false;
    seg->uv[0][42].valid=false;
    double native_phase_u,native_phase_v;
    CHECK(DoomResolutionWallPhase(seg,0,&native_phase_u,&native_phase_v));
    CHECK(fabs(DoomResolutionWallCoordinate(seg,(86.0-108)/108.0)+native_phase_u-11)<1e-9);
    /* Re-capturing a moving door's native texture phase must follow its
     * changed ceiling rather than leaving the artwork fixed in world Z. */
    double closed_phase=native_phase_v;
    seg->uv[0][43].v+=16;
    CHECK(DoomResolutionWallPhase(seg,0,&native_phase_u,&native_phase_v));
    CHECK(fabs(native_phase_v-closed_phase-16)<1e-9);
    /* Rounding in a single newly visible column must not shift an entire
     * wall by a texel. Include wrapped horizontal/vertical samples and
     * remove the centre sample to emulate clipping while moving. */
    DoomResolutionSegment stable={.perpendicular=108,.texture_offset=60,
        .texture_w={63,0},.texture_h={128,0}};
    for(int x=20;x<90;x++) {
        double raw=DoomResolutionWallCoordinate(&stable,(2.0*x-108)/108.0);
        stable.uv[0][x]=(DoomResolutionUv){.valid=true,
            .u=(uint8_t)((int)floor(raw)&63),.v=127.75f,.y=71,.step=1};
    }
    double before_u,before_v,after_u,after_v;
    CHECK(DoomResolutionWallPhase(&stable,0,&before_u,&before_v));
    stable.uv[0][54].valid=false;
    stable.uv[0][55].u=(stable.uv[0][55].u+1)&63;
    CHECK(DoomResolutionWallPhase(&stable,0,&after_u,&after_v));
    CHECK(fabs(remainder(after_u-before_u,64))<0.05);
    CHECK(fabs(remainder(after_v-before_v,128))<1e-9);
    for(int x=20;x<90;x++)stable.uv[0][x].v+=16;
    CHECK(DoomResolutionWallPhase(&stable,0,&after_u,&after_v));
    CHECK(fabs(remainder(after_v-before_v,128)-16)<1e-9);
    *seg=original;
    seg->uv[0][42].v=130;seg->uv[0][43].v=2;
    DoomResolutionWall(scene,rom,0x200000,seg,0,(85.0-108)/DOOM_FOCAL,
        128,64,10,142,144,2,(uint8_t*)out,512*4,palette,visible,NULL);
    CHECK(out[(142+46)*512+10]==(0xff000000u|73u));
    /* Original object-image columns retain transparency and source detail;
     * a nearer wall's depth prevents the object from showing through it. */
    scene->sprite_count=1;
    scene->sprites[0]=(DoomResolutionSprite){.x=0,.depth=1000,.bottom=-10,
        .screen_left=105,.width=64,.height=64,.image=0x1000,.map=0xde};
    for(unsigned u=0;u<64;u++) {
        unsigned table=0x1a1002+u*3,address=0x10000+u*128;
        word(rom+table,address&0xffff);rom[table+2]=0x41;
        for(unsigned y=0;y<64;y++)rom[address+y]=1+(u*7+y)%96;
    }
    double depths[288];for(unsigned y=0;y<288;y++)depths[y]=INFINITY;
    for(unsigned i=0;i<512*448;i++)out[i]=0xff101010;
    DoomResolutionSprites(scene,rom,0x200000,0,1,256,2,(uint8_t*)out,
        512*4,palette,visible,depths);
    CHECK(out[(140+46)*512+256]!=0xff101010);
    /* Native RLTraceO projects height at 128/depth, independently of the
     * horizontal SNES aspect correction. The former square projection
     * omitted this upper part of the sprite. */
    CHECK(out[(131+46)*512+256]!=0xff101010);
    for(unsigned y=0;y<288;y++)depths[y]=10;
    for(unsigned i=0;i<512*448;i++)out[i]=0xff101010;
    DoomResolutionSprites(scene,rom,0x200000,0,1,256,2,(uint8_t*)out,
        512*4,palette,visible,depths);
    for(unsigned y=0;y<448;y++)CHECK(out[y*512+256]==0xff101010);
    /* Preserve native proportions and the supplied left edge at every
     * supported resolution, including the scale-1 mouse-look redraw. */
    uint32_t *scaled=calloc(1024*896,4);CHECK(scaled);
    double scaled_depths[576];
    for(unsigned scale=1;scale<=4;scale++) {
        for(unsigned j=0;j<1024*896;j++)scaled[j]=0xff101010;
        for(unsigned y=0;y<144*scale;y++)scaled_depths[y]=INFINITY;
        DoomResolutionSprites(scene,rom,0x200000,0,1,128*scale,scale,
            (uint8_t*)scaled,1024*4,palette,visible,scaled_depths);
        unsigned painted=0;
        for(unsigned y=0;y<144*scale;y++)
            painted+=scaled[(y+23*scale)*1024+128*scale]!=0xff101010;
        CHECK(painted>=8*scale && painted<=9*scale);
        for(unsigned y=0;y<144*scale;y++)scaled_depths[y]=INFINITY;
        DoomResolutionSprites(scene,rom,0x200000,(104-108)/DOOM_FOCAL,1,
            100*scale,scale,(uint8_t*)scaled,1024*4,palette,visible,scaled_depths);
        for(unsigned y=0;y<144*scale;y++)
            CHECK(scaled[(y+23*scale)*1024+100*scale]==0xff101010);
    }
    /* Floor depth must not hide the lowest sprite pixels. Exercise native
     * and enhanced sizes, close/far objects, yawed views and mouse look. */
    word(scene->sectors+2,0);scene->sectors[8]=200;scene->view_z=32;
    const double distances[]={128,256,1000};
    for(unsigned scale=1;scale<=4;scale++)
    for(unsigned d=0;d<3;d++)
    for(unsigned side=0;side<2;side++)
    for(int look=-20;look<=20;look+=20) {
        scene->horizon_offset=look;
        DoomResolutionSprite *sprite=&scene->sprites[0];
        sprite->depth=distances[d];sprite->bottom=-32;
        sprite->screen_left=108-(int)(32*DOOM_FOCAL/sprite->depth);
        double ratio=side ? 1.3 : 1.0,depth=sprite->depth/ratio;
        int foot=DoomResolutionClipY(-32,depth,scale,0,144*scale,look);
        for(unsigned y=0;y<144*scale;y++)scaled_depths[y]=INFINITY;
        scene->plane_scale=0;
        DoomResolutionPlane(scene,rom,0,false,0,0,128*scale,
            0,144*scale,scale,(uint8_t*)scaled,1024*4,palette,visible,scaled_depths);
        uint32_t expected_plane[576];double expected_depth[576];
        for(unsigned y=0;y<144*scale;y++) {
            expected_plane[y]=scaled[(y+23*scale)*1024+128*scale];
            expected_depth[y]=scaled_depths[y];
        }
        DoomResolutionPreparePlanes(scene,scale);
        DoomResolutionPlane(scene,rom,0,false,0,0,128*scale,
            0,144*scale,scale,(uint8_t*)scaled,1024*4,palette,visible,scaled_depths);
        for(unsigned y=0;y<144*scale;y++) {
            CHECK(expected_plane[y]==scaled[(y+23*scale)*1024+128*scale]);
            CHECK(expected_depth[y]==scaled_depths[y]);
        }
        uint32_t before=scaled[(foot-1+23*scale)*1024+128*scale];
        CHECK(scaled_depths[foot-1]>=depth);
        DoomResolutionSprites(scene,rom,0x200000,0,ratio,128*scale,scale,
            (uint8_t*)scaled,1024*4,palette,visible,scaled_depths);
        CHECK(scaled[(foot-1+23*scale)*1024+128*scale]!=before);
        CHECK(scaled_depths[foot-1]==depth);
    }
    /* Angular bins must agree with the exhaustive intersection oracle,
     * including eye-plane crossings and rays outside the indexed range. */
    scene->count=64;scene->rays_ready=false;scene->ray_limit=3.25;
    for(unsigned i=0;i<scene->count;i++)scene->segments[i]=(DoomResolutionSegment){
        .x1=(int)(i*53%701)-350,.x2=(int)(i*97%601)-300,
        .z1=(int)(i*71%501)-100,.z2=(int)(i*43%701)-80};
    scene->segments[0]=(DoomResolutionSegment){.x1=-1,.x2=1,.z1=1e-8,.z2=1e-8};
    scene->segments[1]=(DoomResolutionSegment){.x1=-1,.x2=1,.z1=-1,.z2=1e-8};
    DoomResolutionHit exhaustive[DOOM_RES_SEGMENTS],indexed[DOOM_RES_SEGMENTS];
    DoomResolutionPrepareRays(scene);
    for(int r=-800;r<=800;r++) {
        double ray=r/200.0;
        unsigned n=DoomResolutionHits(scene,ray,1,indexed);
        scene->rays_ready=false;
        unsigned reference=DoomResolutionHits(scene,ray,1,exhaustive);
        scene->rays_ready=true;
        CHECK(n==reference);
        for(unsigned j=0;j<n;j++)CHECK(indexed[j].segment==exhaustive[j].segment && indexed[j].depth==exhaustive[j].depth);
    }
    /* Removing hits behind the nearest solid wall must keep the exact
     * exhaustive prefix, including shared-depth corners. */
    for(unsigned i=0;i<scene->count;i++)scene->segments[i].flags=i%7==0 ? 1 : 0;
    for(int r=-650;r<=650;r++) {
        double ray=r/200.0;
        unsigned all=DoomResolutionHits(scene,ray,1,exhaustive);
        double stop=INFINITY;
        for(unsigned j=0;j<all;j++)if(scene->segments[exhaustive[j].segment].flags&1) {stop=exhaustive[j].depth;break;}
        unsigned expected=0;while(expected<all && exhaustive[expected].depth<=stop)expected++;
        unsigned n=DoomResolutionCollectHits(scene,ray,1,indexed,true);
        CHECK(n==expected);
        for(unsigned j=0;j<n;j++)CHECK(indexed[j].segment==exhaustive[j].segment && indexed[j].depth==exhaustive[j].depth);
    }
    /* A wall culled in the centre's native two-pixel visibility list can
     * still cover a high-resolution corner. Recover it from a side view
     * without changing its texture anchor, and deduplicate shared walls. */
    DoomResolutionScene *views=calloc(3,sizeof(*views));CHECK(views);
    views[0].count=1;
    views[0].segments[0]=(DoomResolutionSegment){.x1=-8,.x2=8,.z1=128,.z2=128,
        .world_uv=true,.vertex={0x4000,0x4004}};
    views[2].count=1;views[2].segments[0]=views[0].segments[0];
    views[1].count=1;
    DoomResolutionSegment wall={.x1=64,.x2=96,.z1=128,.z2=128,
        .world_uv=true,.offset_x=7,.vertex={0x4008,0x400c}};
    double yaw=-DOOM_SIDE_YAW;
    views[1].segments[0]=wall;
    views[1].segments[0].x1=wall.x1*cos(yaw)-wall.z1*sin(yaw);
    views[1].segments[0].z1=wall.z1*cos(yaw)+wall.x1*sin(yaw);
    views[1].segments[0].x2=wall.x2*cos(yaw)-wall.z2*sin(yaw);
    views[1].segments[0].z2=wall.z2*cos(yaw)+wall.x2*sin(yaw);
    CHECK(DoomResolutionHits(&views[0],0.625,1,indexed)==0);
    DoomResolutionMergeViews(scene,views,NULL,NULL,0);
    CHECK(scene->count==2);
    CHECK(DoomResolutionHits(scene,0.625,1,indexed)==1);
    CHECK(fabs(indexed[0].depth-128)<1e-9);
    CHECK(fabs(DoomResolutionWallCoordinate(&scene->segments[indexed[0].segment],0.625)-23)<1e-9);
    /* A subpixel wall missing from every native camera is recovered from
     * map topology, with the existing patch and exact texture phase. */
    memset(ram,0,0x10000);memset(views,0,3*sizeof(*views));
    ram[0x7e]=0x40;word(ram+0x8e,2);word(ram+0x84,(0x6000-vertex_base)&0xffff);
    word(ram+0x88,0x7000-11);word(ram+0x92,1);word(ram+0x94,0x7100);
    word(rom+0x6000,128);word(rom+0x6002,96);
    word(rom+0x6004,128);word(rom+0x6006,(unsigned)-96);
    word(rom+0x7000,vertex_base);word(rom+0x7002,vertex_base+4);
    rom[0x7004]=1;rom[0x7005]=7;rom[0x7100]=0;rom[0x7101]=6;
    rom[0x7110]=0;rom[0x7111]=6;
    views[0].count=1;views[0].segments[0]=(DoomResolutionSegment){
        .flags=1,.face=0x7110,.texture={0x686},.texture_h={128},.texture_w={127}};
    scene->count=0;scene->view_z=32;scene->ray_limit=3.25;
    word(scene->sectors+2,0);word(scene->sectors+4,64);
    CHECK(DoomResolutionRecoverEdges(scene,views,ram,rom,0x200000)==1);
    CHECK(scene->segments[0].texture[0]==0x686 && scene->segments[0].offset_x==7);
    CHECK(scene->segments[0].flags==(0x8201|0x20|0x40));
    CHECK(fabs(DoomResolutionWallCoordinate(&scene->segments[0],0)-103)<1e-9);
    CHECK(DoomResolutionRecoverEdges(scene,views,ram,rom,0x200000)==0);
    scene->count=0;rom[0x7101]=9;
    CHECK(DoomResolutionRecoverEdges(scene,views,ram,rom,0x200000)==0);
    rom[0x7101]=6;word(rom+0x7000,vertex_base+4);word(rom+0x7002,vertex_base);
    CHECK(DoomResolutionRecoverEdges(scene,views,ram,rom,0x200000)==0);
    /* Retail table decoding works even when no camera saw this texture,
     * and follows the live alternate ID rather than a stale frame cache. */
    word(rom+0x7000,vertex_base);word(rom+0x7002,vertex_base+4);
    memcpy(rom+0x4403,(uint8_t[]){0xa3,0,0xa4,0x5b},4);
    memcpy(rom+0x4467,(uint8_t[]){0xf1,0x36,0x4c},3);
    rom[0x4472]=0xa1;rom[0x4473]=0x7e;
    memset(views,0,3*sizeof(*views));rom[0x7101]=8;
    word(rom+0x1b0008,0x100);rom[0x1b017e]=64;rom[0x1b017f]=63;
    CHECK(DoomResolutionRecoverEdges(scene,views,ram,rom,0x200000)==1);
    CHECK(scene->segments[0].texture[0]==0x17e && scene->segments[0].texture_h[0]==64);
    scene->count=0;rom[0x7004]=0x11;ram[0x4c36+8]=6;
    word(rom+0x1b0006,0x200);rom[0x1b027e]=128;rom[0x1b027f]=127;
    CHECK(DoomResolutionRecoverEdges(scene,views,ram,rom,0x200000)==1);
    CHECK(scene->segments[0].texture[0]==0x27e);
    free(views);
    free(scaled);
    free(out); /* Newly rasterized subpixels, not duplicated native pixels. */
    free(scene);free(ram);free(rom);
    puts("Resolution: bounded RLE, geometry validation, ray depth, subpixel detail and clipping passed");
    return 0;
}
