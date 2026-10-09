#ifndef DOOM_RESOLUTION_H
#define DOOM_RESOLUTION_H
#include "doom_projection.h"
#include "doom_shading.h"
#include "doom_sky.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Retail USA layout, checked before use. These are private BUILD outputs,
 * not framebuffer pixels. Source: DOOM-FX rle.i and rltracew4/5.a. */
enum { DOOM_RES_SEGMENTS = 168, DOOM_RES_SEGMENT_SIZE = 62 };
typedef struct DoomResolutionUv {
    float v, step;
    uint8_t u, y;
    bool valid;
} DoomResolutionUv;
typedef struct DoomResolutionSprite {
    double x, depth, bottom;
    uint16_t image, identity;
    uint8_t width, height, flip, map;
} DoomResolutionSprite;
typedef struct DoomResolutionSegment {
    double x1, z1, x2, z2;
    uint16_t flags, near_sector, far_sector, texture[2];
    uint8_t texture_h[2], texture_w[2], offset_x, offset_y;
    int16_t angle, perpendicular, texture_offset;
    DoomResolutionUv uv[2][108];
} DoomResolutionSegment;
typedef struct DoomResolutionColumn {
    uint32_t key;
    unsigned height;
    bool valid;
    uint8_t pixels[256];
} DoomResolutionColumn;
typedef struct DoomResolutionScene {
    DoomResolutionSegment segments[DOOM_RES_SEGMENTS];
    unsigned count, sprite_count;
    DoomResolutionSprite sprites[28];
    uint8_t sectors[205 * 14];
    int view_z;
    unsigned light_adjust;
    bool invulnerable, sky2;
    DoomResolutionColumn columns[256];
} DoomResolutionScene;
static inline unsigned DoomResolutionWord(const uint8_t *p) {
    return p[0] | ((unsigned)p[1] << 8);
}
static inline int DoomResolutionSigned(const uint8_t *p) {
    return (int16_t)DoomResolutionWord(p);
}
static inline unsigned DoomResolutionRomAddress(unsigned bank, unsigned address) {
    return bank < 0x40 ? ((bank & 0x3f) * 0x8000 + (address & 0x7fff))
                       : ((bank & 0x1f) * 0x10000 + address);
}
static inline bool DoomResolutionCapture(DoomResolutionScene *scene,
    const uint8_t *ram, size_t size, int view_z, const uint8_t *rom, size_t rom_size) {
    scene->count = scene->sprite_count = 0;
    if (!ram || size < 0x10000 || !rom || rom_size < 0x200000 ||
        rom[0x1b0686] != 128 || rom[0x1b0687] != 127) return false;
    unsigned end = DoomResolutionWord(ram + 0xd6);
    if (end < 0x7180 || end > 0x7180 + DOOM_RES_SEGMENTS * DOOM_RES_SEGMENT_SIZE ||
        (end - 0x7180) % DOOM_RES_SEGMENT_SIZE) return false;
    memcpy(scene->sectors, ram + 0x3080, sizeof(scene->sectors));
    unsigned count = 0;
    scene->view_z = view_z;
    scene->light_adjust = ram[0xce];
    scene->invulnerable = DoomResolutionWord(ram + 0x40) != 0;
    scene->sky2 = false;
    for (unsigned a = 0x7180; a < end; a += DOOM_RES_SEGMENT_SIZE) {
        const uint8_t *p = ram + a;
        unsigned v1 = DoomResolutionWord(p + 52), v2 = DoomResolutionWord(p + 54);
        unsigned near_sector = DoomResolutionWord(p + 28);
        if (v1 + 4 > size || v2 + 4 > size || near_sector < 0x3080 ||
            near_sector >= 0x3080 + sizeof(scene->sectors) ||
            (near_sector - 0x3080) % 14) return false;
        DoomResolutionSegment *seg = &scene->segments[count++];
        *seg = (DoomResolutionSegment){0};
        seg->x1 = DoomResolutionSigned(ram + v1 + 2);
        seg->z1 = DoomResolutionSigned(ram + v1);
        seg->x2 = DoomResolutionSigned(ram + v2 + 2);
        seg->z2 = DoomResolutionSigned(ram + v2);
        seg->flags = DoomResolutionWord(p + 4);
        seg->near_sector = (near_sector - 0x3080) / 14;
        unsigned far_sector = DoomResolutionWord(p + 30);
        seg->far_sector = far_sector >= 0x3080 &&
            far_sector < 0x3080 + sizeof(scene->sectors) &&
            (far_sector - 0x3080) % 14 == 0 ? (far_sector - 0x3080) / 14 : UINT16_MAX;
        for (unsigned i = 0; i < 2; i++) {
            seg->texture_h[i] = p[44 + i * 4];
            seg->texture_w[i] = p[45 + i * 4];
            seg->texture[i] = DoomResolutionWord(p + 46 + i * 4);
        }
        seg->offset_x = p[22]; seg->offset_y = p[23];
        seg->angle = (int16_t)DoomResolutionWord(p + 56);
        seg->perpendicular = (int16_t)DoomResolutionWord(p + 58);
        seg->texture_offset = (int16_t)DoomResolutionWord(p + 60);
    }
    scene->count = count;
    return count != 0;
}
/* Wall columns are signed RLE: positive counts have count literals;
 * negative counts repeat the next colour -count times. Bound every read. */
static inline bool DoomResolutionDecodeColumn(const uint8_t *rom, size_t size,
    unsigned address, unsigned height, uint8_t *pixels) {
    unsigned out = 0;
    if (!rom || !pixels || height == 0 || height > 256) return false;
    while (out < height) {
        if (address >= size) return false;
        int run = (int8_t)rom[address++];
        unsigned n = run < 0 ? (unsigned)-run : (unsigned)run;
        if (!n) return false;
        if (n > height - out) n = height - out;
        if (run < 0) {
            if (address >= size) return false;
            memset(pixels + out, rom[address++], n);
        } else {
            if (address > size || n > size - address) return false;
            memcpy(pixels + out, rom + address, n);
            address += n;
        }
        out += n;
    }
    return true;
}
static inline const uint8_t *DoomResolutionTextureColumn(DoomResolutionScene *scene,
    const uint8_t *rom, size_t size, const DoomResolutionSegment *seg,
    unsigned component, int u, unsigned *height) {
    *height = seg->texture_h[component] & 0xfe;
    if (!*height) *height = 256;
    unsigned table = 0x1b0000 + seg->texture[component] + 2 +
        ((unsigned)u & seg->texture_w[component]) * 3;
    if (table + 3 > size) return NULL;
    unsigned address = DoomResolutionRomAddress(rom[table + 2],
        DoomResolutionWord(rom + table));
    DoomResolutionColumn *cache = &scene->columns[(address ^ (address >> 8)) & 255];
    if (!cache->valid || cache->key != address || cache->height != *height) {
        cache->valid = DoomResolutionDecodeColumn(rom, size, address, *height, cache->pixels);
        cache->key = address; cache->height = *height;
    }
    return cache->valid ? cache->pixels : NULL;
}
typedef struct DoomResolutionHit { unsigned segment; double depth; } DoomResolutionHit;
static inline unsigned DoomResolutionHits(const DoomResolutionScene *scene,
    double ray, double depth_ratio, DoomResolutionHit *hits) {
    unsigned n = 0;
    for (unsigned i = 0; i < scene->count; i++) {
        const DoomResolutionSegment *s = &scene->segments[i];
        double dx = s->x2 - s->x1, dz = s->z2 - s->z1;
        double denominator = dx - ray * dz;
        if (fabs(denominator) < 1e-9) continue;
        double t = (ray * s->z1 - s->x1) / denominator;
        double z = s->z1 + t * dz;
        if (t < 0 || t > 1 || z <= 0 || depth_ratio <= 0) continue;
        double depth = z / depth_ratio;
        unsigned j = n;
        while (j && hits[j - 1].depth > depth) { hits[j] = hits[j - 1]; j--; }
        hits[j] = (DoomResolutionHit){i, depth}; n++;
    }
    return n;
}
static inline unsigned DoomResolutionLight(const DoomResolutionScene *scene,
    const uint8_t *sector, double depth, bool floor) {
    unsigned bright = sector[1] > scene->light_adjust ? sector[1] - scene->light_adjust : 0;
    unsigned dark = floor ? (2 * bright + 8 < 247 ? 2 * bright + 8 : 247)
                          : (2 * bright < 255 ? 2 * bright : 255);
    double scale = DOOM_FOCAL / fmax(1, depth);
    double level = scale >= 1 ? bright : dark - scale * (dark - bright);
    return (unsigned)fmax(bright, fmin(dark, level)) >> 3;
}
static inline void DoomResolutionPlane(DoomResolutionScene *scene,
    const uint8_t *rom, unsigned sector_index, bool ceiling,
    unsigned angle, double rx, unsigned x, int from, int to,
    unsigned scale, uint8_t *dst, size_t pitch,
    const uint32_t palettes[144][256], const bool visible[144], double *depths) {
    if (sector_index >= 205) return;
    const uint8_t *sector = scene->sectors + sector_index * 14;
    int height = DoomResolutionSigned(sector + (ceiling ? 4 : 2)) - scene->view_z;
    for (int y = from; y < to; y++) {
        unsigned native_y = (unsigned)y / scale;
        if (!visible[native_y]) continue;
        unsigned color;
        double plane_depth = INFINITY;
        if (ceiling && (sector[0] & 0x80)) {
            color = rom[DoomSkyRomOffset((uint16_t)angle,
                (int)floor(rx + 108), native_y, scene->sky2)];
        } else {
            double sy = ((y + 0.5) / scale - 72);
            double depth = fabs(sy) > 1e-9 ? fabs(height * DOOM_FOCAL / sy) : 7168;
            plane_depth = depth;
            unsigned row = DoomResolutionLight(scene, sector, depth, true);
            row += (((unsigned)floor(rx) ^ native_y) & 1) ? 0 : 1;
            color = rom[0x1cde00 + (scene->invulnerable ? 32 : row) * 256 + sector[8 + ceiling]];
        }
        ((uint32_t *)(dst + (size_t)(y + 23 * scale) * pitch))[x] =
            palettes[native_y][color];
        if (depths) depths[y] = plane_depth;
    }
}
static inline void DoomResolutionWall(DoomResolutionScene *scene,
    const uint8_t *rom, size_t rom_size, const DoomResolutionSegment *seg,
    unsigned component, double source_ray, double depth, int bottom_height,
    unsigned x, int from, int to, unsigned scale, uint8_t *dst, size_t pitch,
    const uint32_t palettes[144][256], const bool visible[144], double *depths) {
    double theta = -atan(source_ray) - seg->angle * (2 * 3.14159265358979323846 / 65536);
    double coordinate = seg->texture_offset - seg->perpendicular * tan(theta) + seg->offset_x;
    int u = isfinite(coordinate) ? (int)fmod(floor(coordinate), seg->texture_w[component] + 1.0) : 0;
    double native_x = 108 + source_ray * DOOM_FOCAL;
    int a = (int)floor(native_x / 2), b = a + 1;
    if (a < 0) a = 0;
    if (b > 107) b = 107;
    while (a >= 0 && !seg->uv[component][a].valid) a--;
    while (b <= 107 && !seg->uv[component][b].valid) b++;
    if (a < 0) a = b;
    if (b > 107) b = a;
    bool native_uv = a >= 0 && b <= 107;
    double centre_v = 0, native_step = depth / DOOM_FOCAL;
    if (native_uv) {
        const DoomResolutionUv *left = &seg->uv[component][a], *right = &seg->uv[component][b];
        double t = a == b ? 0 : fmax(0, fmin(1, (native_x / 2 - a) / (b - a)));
        double ul = left->u, ur = right->u, period = seg->texture_w[component] + 1.0;
        while (ur - ul > period / 2) ur -= period;
        while (ur - ul < -period / 2) ur += period;
        /* Interpolate u/z and 1/z. Endpoint coordinates come directly from
         * native trace commands, retaining its texture phase and pegging. */
        double il = 1 / fmax(1e-6, left->step), ir = 1 / fmax(1e-6, right->step);
        double inverse = (1-t)*il + t*ir;
        u = (int)floor(((1-t)*ul*il + t*ur*ir) / inverse);
        double vl = left->v + (left->y - 71.0)*left->step;
        double vr = right->v + (right->y - 71.0)*right->step;
        unsigned v_period = seg->texture_h[component] & 0xfe;
        if (!v_period) v_period = 256;
        vr = vl + remainder(vr - vl, (double)v_period);
        centre_v = (1-t)*vl + t*vr;
        native_step = depth / DOOM_FOCAL;
    }
    unsigned h;
    const uint8_t *column = DoomResolutionTextureColumn(scene, rom, rom_size, seg, component, u, &h);
    if (!column) return;
    const uint8_t *sector = scene->sectors + seg->near_sector * 14;
    unsigned row = DoomResolutionLight(scene, sector, depth, false);
    for (int y = from; y < to; y++) {
        unsigned native_y = (unsigned)y / scale;
        if (!visible[native_y]) continue;
        double world_z = scene->view_z + (72 - (y + 0.5) / scale) * depth / DOOM_FOCAL;
        double origin = seg->texture_h[component] & 1
            ? DoomResolutionSigned(sector + 4) : bottom_height;
        double texture_v = native_uv
            ? centre_v + (71.5 - (y + 0.5) / scale) * native_step
            : world_z - origin - seg->offset_y;
        unsigned v = (unsigned)(int)floor(texture_v) & (h - 1);
        unsigned color = rom[0x1cde00 + (scene->invulnerable ? 32 : row) * 256 + column[v]];
        ((uint32_t *)(dst + (size_t)(y + 23 * scale) * pitch))[x] =
            palettes[native_y][color];
        if (depths) depths[y] = depth;
    }
}
static inline int DoomResolutionClipY(double height, double depth,
    unsigned scale, int min, int max) {
    double y = ceil((72 - height * DOOM_FOCAL / depth) * scale - 0.5);
    if (y <= min) return min;
    if (y >= max) return max;
    return (int)y;
}
static inline void DoomResolutionRasterColumn(DoomResolutionScene *scene,
    const uint8_t *rom, size_t rom_size, unsigned angle, double rx,
    double source_ray, double depth_ratio, unsigned x, unsigned scale,
    uint8_t *dst, size_t pitch, const uint32_t palettes[144][256],
    const bool visible[144], double *depths) {
    if (depths) for (unsigned y = 0; y < 144 * scale; y++) depths[y] = INFINITY;
    DoomResolutionHit hits[DOOM_RES_SEGMENTS];
    unsigned count = DoomResolutionHits(scene, source_ray, depth_ratio, hits);
    int top = 0, bottom = 144 * scale;
    for (unsigned i = 0; i < count && top < bottom; i++) {
        DoomResolutionSegment *seg = &scene->segments[hits[i].segment];
        const uint8_t *near = scene->sectors + seg->near_sector * 14;
        int floor_z = DoomResolutionSigned(near + 2), ceiling_z = DoomResolutionSigned(near + 4);
        double depth = hits[i].depth;
        int cy = DoomResolutionClipY(ceiling_z - scene->view_z, depth, scale, top, bottom);
        int fy = DoomResolutionClipY(floor_z - scene->view_z, depth, scale, top, bottom);
        DoomResolutionPlane(scene, rom, seg->near_sector, true, angle, rx, x,
            top, cy, scale, dst, pitch, palettes, visible, depths);
        DoomResolutionPlane(scene, rom, seg->near_sector, false, angle, rx, x,
            fy, bottom, scale, dst, pitch, palettes, visible, depths);
        top = cy; bottom = fy;
        if (seg->flags & 1) {
            DoomResolutionWall(scene, rom, rom_size, seg, 0, source_ray, depth,
                ceiling_z, x, top, bottom, scale, dst, pitch, palettes, visible, depths);
            break;
        }
        if (seg->far_sector >= 205) break;
        const uint8_t *far = scene->sectors + seg->far_sector * 14;
        int far_floor = DoomResolutionSigned(far + 2), far_ceiling = DoomResolutionSigned(far + 4);
        if (far_ceiling < ceiling_z) {
            int end = DoomResolutionClipY(far_ceiling - scene->view_z, depth, scale, top, bottom);
            if (seg->flags & 2) DoomResolutionWall(scene, rom, rom_size, seg, 0,
                source_ray, depth, ceiling_z, x, top, end, scale, dst, pitch, palettes, visible, depths);
            top = end;
        }
        if (far_floor > floor_z) {
            int start = DoomResolutionClipY(far_floor - scene->view_z, depth, scale, top, bottom);
            if (seg->flags & 4) DoomResolutionWall(scene, rom, rom_size, seg, 1,
                source_ray, depth, far_floor, x, start, bottom, scale, dst, pitch, palettes, visible, depths);
            bottom = start;
        }
    }
}
/* Read the fully resolved native WALLPLOT records after each private build.
 * Records for the three strips share immutable segment identity. */
static inline void DoomResolutionCaptureUv(DoomResolutionScene *scene,
    const uint8_t *ram, size_t size, const uint8_t *rom, size_t rom_size) {
    for (unsigned i = 0; i < scene->count; i++) {
        const uint8_t *p = ram + 0x7180 + i * 62;
        unsigned start = DoomResolutionWord(p + 40), end = DoomResolutionWord(p + 42);
        if (end < start || end > size || (end - start) % 14) continue;
        for (unsigned a = start; a + 14 <= end; a += 14) {
            unsigned component = ram[a + 10] == 48 ? 1 : 0, x = ram[a + 11];
            if (x >= 108 || (ram[a + 10] != 44 && ram[a + 10] != 48)) continue;
            DoomResolutionSegment *seg = &scene->segments[i];
            unsigned column_address = DoomResolutionRomAddress(ram[a + 5],
                DoomResolutionWord(ram + a + 6));
            unsigned u, base = 0x1b0000 + seg->texture[component] + 2;
            for (u = 0; u <= seg->texture_w[component]; u++) {
                unsigned table = base + u * 3;
                if (table + 3 > rom_size) break;
                if (DoomResolutionRomAddress(rom[table + 2],
                    DoomResolutionWord(rom + table)) == column_address) break;
            }
            if (u > seg->texture_w[component]) continue;
            seg->uv[component][x] = (DoomResolutionUv){
                .u=(uint8_t)u, .v=ram[a + 9]+ram[a + 8]/256.0f,
                .step=DoomResolutionWord(ram+a+2)/256.0f,
                .y=ram[a+4], .valid=true };
        }
    }
}
/* Capture actual selected object frames, flipping and native colour maps,
 * including glow/invulnerability. No object AI or animation is reconstructed. */
static inline void DoomResolutionCaptureSprites(DoomResolutionScene *scene,
    const uint8_t *ram, size_t size, const uint8_t *rom, size_t rom_size) {
    unsigned address = DoomResolutionWord(ram + 0xda);
    for (unsigned visits = 0; address && visits < 28; visits++) {
        if (address + 26 > size) break;
        const uint8_t *p = ram + address;
        unsigned identity = DoomResolutionWord(p + 4);
        unsigned i;
        for (i=0;i<scene->sprite_count;i++)
            if (scene->sprites[i].identity == identity) break;
        if (i == 28) break;
        if (i == scene->sprite_count) scene->sprite_count++;
        DoomResolutionSprite *sprite = &scene->sprites[i];
        unsigned image = DoomResolutionWord(p + 18);
        if (0x1a0000 + image + 2 > rom_size) break;
        sprite->identity=identity;
        sprite->image=image;
        sprite->depth=DoomResolutionSigned(p);
        sprite->x=DoomResolutionSigned(p+6);
        sprite->bottom=DoomResolutionSigned(p+8);
        sprite->height=p[14];sprite->width=p[15];
        sprite->flip=p[21];
        unsigned start=DoomResolutionWord(p+22), end=DoomResolutionWord(p+24);
        if (start + 10 <= end && end <= size) sprite->map=ram[start+7];
        address=DoomResolutionWord(p+2);
    }
    for (unsigned i=1;i<scene->sprite_count;i++) {
        DoomResolutionSprite sprite=scene->sprites[i];
        unsigned j=i;
        while(j && scene->sprites[j-1].depth<sprite.depth) {
            scene->sprites[j]=scene->sprites[j-1];j--;
        }
        scene->sprites[j]=sprite;
    }
}
static inline bool DoomResolutionDecodeSprite(const uint8_t *rom, size_t size,
    unsigned address, unsigned height, uint8_t *pixels) {
    if (!rom || !pixels || !height || height > 256) return false;
    memset(pixels,0,height);
    unsigned y=0;
    while(y<height) {
        if(address>=size)return false;
        unsigned color=rom[address++];
        if(color) { pixels[y++]=(uint8_t)color; continue; }
        if(address>=size)return false;
        int n=(int8_t)rom[address++];
        if(!n)return true;
        unsigned length=n<0 ? (unsigned)-n : (unsigned)n;
        if(length>height-y)length=height-y;
        if(n<0) {
            if(address>=size)return false;
            memset(pixels+y,rom[address++],length);
        }
        y+=length;
    }
    return true;
}
static inline const uint8_t *DoomResolutionSpriteColumn(DoomResolutionScene *scene,
    const uint8_t *rom, size_t size, const DoomResolutionSprite *sprite, unsigned u) {
    if(u>=sprite->width || !sprite->height)return NULL;
    if(sprite->flip)u=sprite->width-1-u;
    unsigned table=0x1a0000+sprite->image+2+u*3;
    if(table+3>size)return NULL;
    unsigned address=DoomResolutionRomAddress(rom[table+2],DoomResolutionWord(rom+table));
    unsigned key=address | 0x80000000u;
    DoomResolutionColumn *cache=&scene->columns[(address^(address>>8))&255];
    if(!cache->valid || cache->key!=key || cache->height!=sprite->height) {
        cache->valid=DoomResolutionDecodeSprite(rom,size,address,sprite->height,cache->pixels);
        cache->key=key;cache->height=sprite->height;
    }
    return cache->valid ? cache->pixels : NULL;
}
static inline void DoomResolutionSprites(DoomResolutionScene *scene,
    const uint8_t *rom,size_t size,double source_ray,double ratio,unsigned x,
    unsigned scale,uint8_t *dst,size_t pitch,const uint32_t palettes[144][256],
    const bool visible[144],double *depths) {
    for(unsigned i=0;i<scene->sprite_count;i++) {
        DoomResolutionSprite *sprite=&scene->sprites[i];
        if(sprite->depth<=0 || !sprite->width || !sprite->height ||
            sprite->map<0xde || sprite->map>0xfe)continue;
        double u=source_ray*sprite->depth-sprite->x+sprite->width/2.0;
        double footprint=sprite->depth/(DOOM_FOCAL*scale);
        double spread=footprint>1 ? footprint/4 : 0;
        if(u<-spread || u>=sprite->width+spread)continue;
        double depth=sprite->depth/ratio;
        double top=(72-(sprite->bottom+sprite->height)*DOOM_FOCAL/depth)*scale;
        double bottom=(72-sprite->bottom*DOOM_FOCAL/depth)*scale;
        int first=(int)fmax(0,fmin(144*scale,ceil(top-0.5)));
        int last=(int)fmax(0,fmin(144*scale,ceil(bottom-0.5)));
        for(int y=first;y<last;y++) {
            unsigned ny=y/scale;
            if(!visible[ny] || depth>depths[y]+0.001)continue;
            double v=(71.5-(y+0.5)/scale)*depth/DOOM_FOCAL-sprite->bottom;
            uint32_t *target=&((uint32_t*)(dst+(size_t)(y+23*scale)*pitch))[x];
            unsigned red=0,green=0,blue=0,covered=0;
            unsigned taps=spread>0 ? 4 : 1;
            for(unsigned tap=0;tap<taps;tap++) {
                int tu=(int)floor(u+(tap&1 ? spread : -spread));
                int tv=(int)floor(v+(tap&2 ? spread : -spread));
                unsigned raw=0;
                if(tu>=0 && tu<sprite->width && tv>=0 && tv<sprite->height) {
                    const uint8_t *column=DoomResolutionSpriteColumn(scene,rom,size,sprite,tu);
                    if(column)raw=column[tv];
                }
                uint32_t color=*target;
                if(raw) {
                    unsigned mapped=rom[0x1c0000+(sprite->map<<8)+raw];
                    color=palettes[ny][mapped];covered++;
                }
                red+=(color>>16)&255;green+=(color>>8)&255;blue+=color&255;
            }
            if(covered) {
                *target=0xff000000u|((red/taps)<<16)|((green/taps)<<8)|(blue/taps);
                depths[y]=depth;
            }
        }
    }
}
#endif
