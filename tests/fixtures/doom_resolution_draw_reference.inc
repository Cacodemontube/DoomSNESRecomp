/* Frozen full-world raster and native overlay, before covered-row optimization. */
static void DrawResolutionRangeReference(uint8_t *dst,size_t pitch,unsigned width,unsigned scale,
                                unsigned first,unsigned last,bool wide)
{
    /* ROM texture columns are immutable. Keep each raster thread's cache
     * across presentations instead of decoding them again every batch. */
    static _Thread_local DoomResolutionColumn columns_cache[256];
    static _Thread_local const uint8_t *columns_rom;
    static _Thread_local size_t columns_rom_size;
    static _Thread_local uint64_t columns_epoch;
    if(columns_rom!=s.snapshot.rom || columns_rom_size!=s.snapshot.rom_size || columns_epoch!=texture_cache_epoch) {
        memset(columns_cache,0,sizeof(columns_cache));
        columns_rom=s.snapshot.rom;columns_rom_size=s.snapshot.rom_size;
        columns_epoch=texture_cache_epoch;
    }
    doom_resolution_batch_columns=columns_cache;
    unsigned columns = scale == 1 && wide ? 2 : 1;
    for (unsigned x = first; x < last; x += columns) {
        double rx = (x + columns * 0.5) / scale - width / 2.0;
        unsigned view = 0;
        double yaw = 0;
        if (rx < -108 || rx >= 108) {
            view = rx < 0 ? 1 : 2;
            yaw = rx < 0 ? -DOOM_SIDE_YAW : DOOM_SIDE_YAW;
        }
        double sx, sy;
        DoomProjectRay(rx, 0, yaw, &sx, &sy);
        double ratio = cos(yaw) + rx / DOOM_FOCAL * sin(yaw);
        DoomResolutionScene *scene = &s.resolution_scenes[view];
        double depths[144 * 4];
        DoomResolutionScene *geometry=scale>1 ? &s.geometry_scene : scene;
        DoomResolutionRasterColumnRetained(geometry, s.snapshot.rom, s.snapshot.rom_size,
            s.cached_camera.angle, rx, geometry==scene ? (sx - 108) / DOOM_FOCAL : rx/DOOM_FOCAL,
            geometry==scene ? ratio : 1,
            x, scale, dst, pitch, s.palettes, s.visible_rows, depths,true);
        if (scale > 1 || (wide && view != 0))
            DoomResolutionSprites(wide ? &s.geometry_scene : scene, s.snapshot.rom, s.snapshot.rom_size,
                wide ? rx / DOOM_FOCAL : (sx - 108) / DOOM_FOCAL,
                wide ? 1 : ratio, x, scale, dst, pitch,
                s.palettes, s.visible_rows, depths);
        if (columns == 2 && x + 1 < last)
            for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
                if (!s.visible_rows[y]) continue;
                uint32_t *line = (uint32_t *)(dst + (y + kNativeViewY) * pitch);
                line[x + 1] = line[x];
            }
    }
    doom_resolution_batch_columns=NULL;
}

bool DoomRendererDrawResolutionReference(uint8_t *dst, size_t pitch,
                                unsigned width, unsigned scale)
{
    if (!dst || scale < 1 || scale > 4 || width < 256 ||
        width > 684 || pitch < width * scale * 4 || !s.cached) return false;
    bool wide = s.widescreen && width > 256;
    unsigned x0 = wide ? 20 : (width - 256) / 2 + 20;
    unsigned x1 = wide ? width - 20 : x0 + 216;
    /* Interpolation often presents the same quantized camera several times.
     * Reuse only the world; weapons, HUD, messages and map are composed later.
     * Beam palettes/row visibility and mouse pitch can change between guest
     * captures, so they are part of the cache identity as well. */
    size_t row_bytes=(x1-x0)*scale*4;
    if(scale>1 && !getenv("DOOM_DISABLE_WORLD_CACHE") && s.resolution_cache_valid &&
       s.resolution_cache_width==width && s.resolution_cache_scale==scale &&
       s.resolution_cache_horizon==s.horizon_offset &&
       !memcmp(s.resolution_cache_palettes,s.palettes,sizeof(s.palettes)) &&
       !memcmp(s.resolution_cache_rows,s.visible_rows,sizeof(s.visible_rows))) {
        for(unsigned y=0;y<DOOM_VIEW_HEIGHT*scale;y++)
            if(s.visible_rows[y/scale])memcpy(dst+(y+kNativeViewY*scale)*pitch+x0*scale*4,
                s.resolution_cache+y*row_bytes,row_bytes);
        return true;
    }
    if(scale>1) {
        /* Preserve the original hall-of-mirrors effect in the output's own
         * resolution. Never seed it from the magnified native framebuffer. */
        bool history=s.resolution_cache && s.resolution_cache_capacity>=row_bytes*144*scale &&
            s.resolution_cache_width==width && s.resolution_cache_scale==scale;
        for(unsigned y=0;y<144*scale;y++)if(s.visible_rows[y/scale]) {
            uint32_t *row=(uint32_t *)(dst+(y+kNativeViewY*scale)*pitch)+x0*scale;
            if(history && s.resolution_cache_rows[y/scale])memcpy(row,s.resolution_cache+y*row_bytes,row_bytes);
            else for(unsigned x=0;x<(x1-x0)*scale;x++)row[x]=0xff000000;
        }
    }
    if(scale>1 && !s.geometry_ready) {
        DoomResolutionMergeViews(&s.geometry_scene,s.resolution_scenes,s.work_ram,
            s.snapshot.rom,s.snapshot.rom_size);
        s.stats.recovered_edges=DoomResolutionRecoverEdges(&s.geometry_scene,s.resolution_scenes,s.work_ram,
            s.snapshot.rom,s.snapshot.rom_size);
        DoomResolutionPrepareRays(&s.geometry_scene);
        s.geometry_ready=true;
    }
    if(scale>1) {
        s.geometry_scene.horizon_offset=s.horizon_offset;
        DoomResolutionPreparePlanes(&s.geometry_scene,scale);
    }
    if(wide) {
        DoomResolutionMergeSprites(&s.geometry_scene,s.resolution_scenes);
        s.geometry_scene.horizon_offset=s.horizon_offset;
    }
    if (wide && scale<=1) {
        /* Sector and vertex identity identify the same native wall in all
         * visibility cameras. Prefer the central camera's texture anchor
         * and share it at both joins, including the current door position. */
        for (unsigned view=0;view<3;view++)
        for (unsigned i=0;i<s.resolution_scenes[view].count;i++) {
            DoomResolutionSegment *seg=&s.resolution_scenes[view].segments[i];
            if(seg->world_uv)continue;
            for(unsigned component=0;component<2;component++) {
                seg->phase_valid[component]=false;
                for(unsigned ref=0;ref<3 && !seg->phase_valid[component];ref++)
                for(unsigned j=0;j<s.resolution_scenes[ref].count;j++) {
                    const DoomResolutionSegment *other=&s.resolution_scenes[ref].segments[j];
                    if(seg->near_sector!=other->near_sector || seg->vertex[0]!=other->vertex[0] ||
                       seg->vertex[1]!=other->vertex[1] || seg->texture[component]!=other->texture[component])continue;
                    if(DoomResolutionWallPhase(other,component,&seg->phase_u[component],&seg->phase_v[component])) {
                        seg->phase_valid[component]=true;break;
                    }
                }
            }
        }
    }
    /* Original low detail draws pairs of columns. Sample a common flat
     * camera at that same density across the complete widescreen view,
     * rather than magnifying the side cameras' framebuffer pixels. */
    for(unsigned view=0;view<3;view++) {
        s.resolution_scenes[view].horizon_offset=s.horizon_offset;
        s.resolution_scenes[view].continuous_uv=wide;
        if(s.resolution_scenes[view].count && scale<=1) {
            DoomResolutionPrepareRays(&s.resolution_scenes[view]);
            DoomResolutionPreparePlanes(&s.resolution_scenes[view],scale);
        }
    }
#ifndef DOOM_RENDERER_SYNCHRONOUS
    if(scale>1 && EnsureWorkers()) {
        unsigned first=x0*scale,last=x1*scale,span=last-first;
        for(unsigned i=0;i<2;i++) {
            RenderWorker *worker=&s.workers[i];
            worker->raster=true;worker->dst=dst;worker->pitch=pitch;
            worker->width=width;worker->scale=scale;
            worker->first=first+span*i/3;worker->last=first+span*(i+1)/3;
            DoomSignalSemaphore(worker->request);
        }
        DrawResolutionRangeReference(dst,pitch,width,scale,first+span*2/3,last,wide);
        for(unsigned i=0;i<2;i++)DoomWaitSemaphore(s.workers[i].done);
    } else
#endif
        DrawResolutionRangeReference(dst,pitch,width,scale,x0*scale,x1*scale,wide);
    if(scale>1) {
        size_t bytes=row_bytes*DOOM_VIEW_HEIGHT*scale;
        if(s.resolution_cache_capacity<bytes) {
            uint8_t *cache=realloc(s.resolution_cache,bytes);
            if(cache) { s.resolution_cache=cache;s.resolution_cache_capacity=bytes; }
        }
        s.resolution_cache_valid=s.resolution_cache_capacity>=bytes;
        if(s.resolution_cache_valid) {
            for(unsigned y=0;y<DOOM_VIEW_HEIGHT*scale;y++)
                memcpy(s.resolution_cache+y*row_bytes,
                    dst+(y+kNativeViewY*scale)*pitch+x0*scale*4,row_bytes);
            memcpy(s.resolution_cache_palettes,s.palettes,sizeof(s.palettes));
            memcpy(s.resolution_cache_rows,s.visible_rows,sizeof(s.visible_rows));
            s.resolution_cache_width=width;s.resolution_cache_scale=scale;
            s.resolution_cache_horizon=s.horizon_offset;
        }
    }
    if (scale == 1) {
        /* Low-resolution mouse look translates the native Super FX image.
         * Keep its object size, rounding, column duplication and transparent
         * texels instead of reconstructing billboards with continuous math.
         * The raster above supplies only the newly exposed edge rows. */
        BuildProjectionMap(width, wide, x0, x1);
        int shift = (int)lround(s.horizon_offset);
        for (unsigned y = 0; y < DOOM_VIEW_HEIGHT; y++) {
            int source_y = (int)y - shift;
            if (!s.visible_rows[y] || source_y < 0 || source_y >= (int)DOOM_VIEW_HEIGHT)
                continue;
            uint32_t *line = (uint32_t *)(dst + (y + kNativeViewY) * pitch);
            for (unsigned x = x0; x < x1; x++) {
                unsigned picture = s.map_picture[x];
                unsigned offset = s.map_y[source_y][x] * DOOM_VIEW_WIDTH + s.map_x[x];
                if(picture==0 && s.message_pixels[offset])continue;
                /* In widescreen, retain only original central sprite pixels.
                 * World geometry and side sprites use the common projection. */
                if (wide && (picture != 0 || !s.object_pixels[0][offset])) continue;
                line[x] = s.palettes[y][s.pictures[picture][offset]];
            }
        }
    }
    return true;
}

