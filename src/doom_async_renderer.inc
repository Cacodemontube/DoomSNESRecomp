/* Native-view replay pipeline. Included after the synchronous job executor.
 * The worker owns ROM/endpoints/output and never reads live emulator state.
 * The host publishes complete results only, and drops obsolete generations. */
#ifndef DOOM_RENDERER_SYNCHRONOUS
#if SNESRECOMP_SDL3
typedef SDL_AtomicInt AsyncStopFlag;
#define AsyncGetStop SDL_GetAtomicInt
#define AsyncSetStop SDL_SetAtomicInt
#else
typedef SDL_atomic_t AsyncStopFlag;
#define AsyncGetStop SDL_AtomicGet
#define AsyncSetStop SDL_AtomicSet
#endif
typedef struct AsyncPicture {
    uint8_t pixels[DOOM_VIEW_WIDTH * DOOM_VIEW_HEIGHT];
    uint16_t floors[DOOM_VIEW_WIDTH * DOOM_VIEW_HEIGHT];
    uint8_t objects[DOOM_VIEW_WIDTH * DOOM_VIEW_HEIGHT];
    uint8_t messages[DOOM_VIEW_WIDTH * DOOM_VIEW_HEIGHT];
    uint8_t floor_used[kMaxSectors];
    DoomResolutionScene scene;
} AsyncPicture;

static struct {
    SDL_Thread *thread;
    DoomSemaphore *request, *done;
    AsyncStopFlag shutdown;
    bool busy, have_picture, unavailable;
    unsigned epoch, job_epoch, generation, published_generation;
    uint64_t started, last_progress;
    unsigned test_delay_ms;
    uint64_t scene_key, published_key;
    Camera published_camera;
    bool published_metadata;
    size_t ram_size, rom_size;
    uint8_t *ram, *previous, *work, *rom, *oracle_work;
    AsyncPicture *picture, *oracle;
    RenderJob job;
} async_renderer;
static bool async_disabled;
static uint64_t async_retry_at;
#ifdef DOOM_DIAGNOSTIC_BUILD
static bool async_test_delay_consumed;
#endif

static void TraceAsyncCamera(const char *event, Camera camera, unsigned generation)
{
    if (getenv("DOOM_CAMERA_TRACE"))
        fprintf(stderr, "[doom-camera] event=%s field=%u generation=%u x=%d y=%d z=%d angle=%u\n",
                event, s.field, generation, camera.x, camera.y, camera.z, camera.angle);
}

static bool AsyncRequested(void)
{
    const char *override = getenv("DOOM_ASYNC_PRESENTATION");
#ifdef DOOM_ASYNC_PRESENTATION_DEFAULT
    bool requested = true;
#else
    bool requested = false;
#endif
    if (override) requested = atoi(override) != 0;
    if (!requested || async_disabled) return false;
    if (async_retry_at) {
        if (SDL_GetPerformanceCounter() < async_retry_at) return false;
        async_retry_at = 0;
        s.stats.async_retries++;
        /* The synchronous fallback may have used displayed-pose rebasing.
         * Start the renewed native timeline at a complete endpoint. */
        s.has_previous = s.cached = false;
        fputs("[doom-async-retry] restarting camera worker after temporary overload\n", stderr);
    }
    return true;
}

static int AsyncRendererMain(void *unused)
{
    (void)unused;
    /* Normal priority guarantees progress alongside other CPU-bound tasks. */
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_NORMAL);
    for (;;) {
        DoomWaitSemaphore(async_renderer.request);
        if (AsyncGetStop(&async_renderer.shutdown)) return 0;
        if (async_renderer.test_delay_ms) SDL_Delay(async_renderer.test_delay_ms);
        ExecuteRenderJob(&async_renderer.job);
        DoomSignalSemaphore(async_renderer.done);
    }
}

static void InvalidateAsyncPicture(void)
{
    async_renderer.epoch++;
    async_renderer.have_picture = false;
    async_renderer.last_progress = 0;
}

static void StopAsyncRenderer(void)
{
    if (async_renderer.thread) {
        AsyncSetStop(&async_renderer.shutdown, 1);
        DoomSignalSemaphore(async_renderer.request);
        SDL_WaitThread(async_renderer.thread, NULL);
        if (async_renderer.busy) {
            if (DoomTryWaitSemaphore(async_renderer.done)) {
                MergeRenderJob(&async_renderer.job);
                if (!async_renderer.job.success) s.stats.failures++;
                s.stats.async_stale++;
            } else s.stats.async_cancelled++;
        }
    }
    if (async_renderer.request) SDL_DestroySemaphore(async_renderer.request);
    if (async_renderer.done) SDL_DestroySemaphore(async_renderer.done);
    free(async_renderer.ram); free(async_renderer.previous); free(async_renderer.work);
    free(async_renderer.rom); free(async_renderer.picture);
    free(async_renderer.oracle); free(async_renderer.oracle_work);
    memset(&async_renderer, 0, sizeof(async_renderer));
}

static bool EnsureAsyncRenderer(void)
{
    if (async_renderer.unavailable) return false;
    if (async_renderer.thread) return true;
    async_renderer.ram_size = s.snapshot.ram_size;
    async_renderer.rom_size = s.snapshot.rom_size;
    async_renderer.ram = malloc(async_renderer.ram_size);
    async_renderer.previous = malloc(async_renderer.ram_size);
    async_renderer.work = malloc(async_renderer.ram_size);
    async_renderer.rom = malloc(async_renderer.rom_size);
    async_renderer.picture = calloc(1, sizeof(*async_renderer.picture));
    async_renderer.request = SDL_CreateSemaphore(0);
    async_renderer.done = SDL_CreateSemaphore(0);
    const char *verify = getenv("DOOM_ASYNC_VERIFY");
#ifdef DOOM_DIAGNOSTIC_BUILD
    const char *delay = getenv("DOOM_ASYNC_TEST_DELAY_MS");
    const char *once = getenv("DOOM_ASYNC_TEST_DELAY_ONCE");
    if (delay && !(once && atoi(once) && async_test_delay_consumed)) {
        char *end; unsigned long value = strtoul(delay, &end, 10);
        if (end != delay && !*end && value <= 2000) async_renderer.test_delay_ms = (unsigned)value;
        if (async_renderer.test_delay_ms) async_test_delay_consumed = true;
    }
#endif
    if (verify && atoi(verify)) {
        async_renderer.oracle = calloc(1, sizeof(*async_renderer.oracle));
        async_renderer.oracle_work = malloc(async_renderer.ram_size);
    }
    if (async_renderer.ram && async_renderer.previous && async_renderer.work &&
        async_renderer.rom && async_renderer.picture && async_renderer.request && async_renderer.done &&
        (!(verify && atoi(verify)) || (async_renderer.oracle && async_renderer.oracle_work)))
        async_renderer.thread = SDL_CreateThread(AsyncRendererMain, "doom-camera", NULL);
    if (!async_renderer.thread) {
        StopAsyncRenderer();
        async_renderer.unavailable = true;
        s.stats.async_fallbacks++;
        fputs("[doom-async-fallback] worker resources unavailable; using synchronous renderer\n", stderr);
        return false;
    }
    async_renderer.last_progress = 0;
    return true;
}

static void BindAsyncOutputs(RenderJob *job, AsyncPicture *picture, uint8_t *work)
{
    job->picture = picture->pixels; job->ram = work;
    job->message_buffer = picture->messages;
    job->messages = job->inputs.metadata ? picture->messages : NULL;
    job->floors = job->inputs.metadata ? picture->floors : NULL;
    job->floor_used = job->inputs.metadata ? picture->floor_used : NULL;
    job->objects = job->inputs.metadata ? picture->objects : NULL;
    job->resolution_scene = job->inputs.metadata ? &picture->scene : NULL;
}

static bool VerifyAsyncJob(void)
{
    if (!async_renderer.oracle) return true;
    RenderJob expected = async_renderer.job;
    expected.third = 0; expected.instructions = 0; expected.success = false;
#ifdef DOOM_PROFILE_RENDERER
    expected.profile_build = expected.profile_draw = expected.profile_metadata = expected.profile_decode = 0;
#endif
    BindAsyncOutputs(&expected, async_renderer.oracle, async_renderer.oracle_work);
    ExecuteRenderJob(&expected);
    bool equal = expected.success == async_renderer.job.success &&
        expected.instructions == async_renderer.job.instructions &&
        !memcmp(async_renderer.work, async_renderer.oracle_work, async_renderer.ram_size) &&
        !memcmp(async_renderer.picture->pixels, async_renderer.oracle->pixels, sizeof(async_renderer.picture->pixels)) &&
        !memcmp(async_renderer.picture->messages, async_renderer.oracle->messages, sizeof(async_renderer.picture->messages));
    if (equal && expected.inputs.metadata) {
        const AsyncPicture *a = async_renderer.picture, *b = async_renderer.oracle;
        equal = !memcmp(a->floors, b->floors, sizeof(a->floors)) &&
            !memcmp(a->objects, b->objects, sizeof(a->objects)) &&
            !memcmp(a->floor_used, b->floor_used, sizeof(a->floor_used)) &&
            a->scene.count == b->scene.count && a->scene.sprite_count == b->scene.sprite_count &&
            a->scene.view_z == b->scene.view_z && a->scene.light_adjust == b->scene.light_adjust &&
            a->scene.invulnerable == b->scene.invulnerable && a->scene.sky2 == b->scene.sky2 &&
            a->scene.continuous_uv == b->scene.continuous_uv && a->scene.ray_limit == b->scene.ray_limit &&
            a->scene.horizon_offset == b->scene.horizon_offset &&
            !memcmp(a->scene.sectors, b->scene.sectors, sizeof(a->scene.sectors)) &&
            !memcmp(a->scene.segments, b->scene.segments, a->scene.count * sizeof(*a->scene.segments)) &&
            !memcmp(a->scene.sprites, b->scene.sprites, a->scene.sprite_count * sizeof(*a->scene.sprites));
    }
    if (equal) s.stats.async_verified++;
    else {
        s.stats.async_mismatches++;
        fputs("[doom-async-mismatch] worker differs from synchronous replay; disabling pipeline\n", stderr);
        async_disabled = true;
        s.stats.async_fallbacks++;
    }
    return equal;
}

static bool RenderAsync(Camera camera, double fraction, uint64_t scene_key, bool *available)
{
    *available = false;
    if (!EnsureAsyncRenderer()) return false;
    bool published = false;
    if (async_renderer.busy && DoomTryWaitSemaphore(async_renderer.done)) {
        async_renderer.busy = false;
        MergeRenderJob(&async_renderer.job);
        if (!VerifyAsyncJob()) return false;
        if (!async_renderer.job.success) {
            s.stats.failures++;
            async_disabled = true;
            s.stats.async_fallbacks++;
            return false;
        }
        /* A native capture may arrive while a completed job awaits a video
         * slot. Permit one endpoint of latency, but never publish backwards
         * or across an invalidated scene. Exact-generation acceptance starves
         * the pipeline precisely when simulation consumes the video budget. */
        if (async_renderer.job_epoch == async_renderer.epoch &&
            s.generation - async_renderer.generation <= 1 &&
            async_renderer.generation >= async_renderer.published_generation &&
            (async_renderer.job.inputs.metadata || !NeedsResolutionScene())) {
            const AsyncPicture *picture = async_renderer.picture;
            memcpy(s.pictures[0], picture->pixels, sizeof(picture->pixels));
            memcpy(s.work_ram, async_renderer.work, async_renderer.ram_size);
            memcpy(s.presented_ram, async_renderer.work, async_renderer.ram_size);
            memcpy(s.message_pixels, picture->messages, sizeof(picture->messages));
            if (async_renderer.job.inputs.metadata) {
                s.resolution_scenes[0] = picture->scene;
                for (unsigned i=0;i<s.resolution_scenes[0].sprite_count;i++)
                    DoomResolutionRotateSprite(&s.resolution_scenes[0].sprites[i],
                        s.work_ram,async_renderer.ram_size,async_renderer.rom,async_renderer.rom_size);
                memcpy(s.floors[0], picture->floors, sizeof(picture->floors));
                memcpy(s.object_pixels[0], picture->objects, sizeof(picture->objects));
                memcpy(s.floor_used[0], picture->floor_used, sizeof(picture->floor_used));
            } else s.resolution_scenes[0].count = s.resolution_scenes[0].sprite_count = 0;
            s.resolution_scenes[1].count = s.resolution_scenes[2].count = 0;
            s.resolution_scenes[1].sprite_count = s.resolution_scenes[2].sprite_count = 0;
            s.resolution_cache_valid = s.geometry_ready = false;
            async_renderer.published_camera = async_renderer.job.camera;
            async_renderer.published_key = async_renderer.scene_key;
            async_renderer.published_generation = async_renderer.generation;
            async_renderer.published_metadata = async_renderer.job.inputs.metadata;
            async_renderer.have_picture = true;
            s.stats.async_accepted++;
            TraceAsyncCamera("publish", async_renderer.published_camera,
                             async_renderer.published_generation);
            async_renderer.last_progress = 0;
            published = true;
        } else {
            s.stats.async_stale++;
            /* A very slow worker must not leave a frozen world forever. */
            if (!async_renderer.last_progress)
                async_renderer.last_progress = SDL_GetPerformanceCounter();
            if ((SDL_GetPerformanceCounter() - async_renderer.last_progress) /
                (double)SDL_GetPerformanceFrequency() > 1.0) {
                fputs("[doom-async-fallback] repeated obsolete jobs; using synchronous renderer for two seconds\n", stderr);
                s.stats.async_fallbacks++;
                s.stats.async_temporary_fallbacks++;
                /* This job finished successfully. Discard it rather than
                 * weakening geometry/history checks, then retry fresh inputs
                 * after a bounded cooldown. A transient overload must not
                 * disable the fast path for the rest of a long session. */
                StopAsyncRenderer();
                async_retry_at = SDL_GetPerformanceCounter() + 2 * SDL_GetPerformanceFrequency();
                s.cached = s.has_presented = false;
                return false;
            }
        }
    }
    if (async_renderer.busy &&
        (SDL_GetPerformanceCounter() - async_renderer.started)/(double)SDL_GetPerformanceFrequency() > 1.0) {
        fputs("[doom-async-fallback] camera worker exceeded one second; using synchronous renderer\n", stderr);
        s.stats.async_fallbacks++;
        async_disabled = true;
        return false;
    }
    if (async_renderer.have_picture) {
        s.cached = true;
        s.cached_camera = async_renderer.published_camera;
        s.cached_scene_key = async_renderer.published_key;
        s.cached_generation = async_renderer.published_generation;
        s.cached_resolution_scene = async_renderer.published_metadata;
        *available = async_renderer.published_metadata || !NeedsResolutionScene();
        if (*available && !published) s.stats.async_reused++;
    }
    bool changed = !async_renderer.have_picture || s.generation != async_renderer.published_generation ||
        memcmp(&camera, &async_renderer.published_camera, sizeof(camera)) ||
        scene_key != async_renderer.published_key || NeedsResolutionScene() != async_renderer.published_metadata;
    if (!async_renderer.busy && changed) {
        RenderJob *job = &async_renderer.job;
        memset(job, 0, sizeof(*job));
        job->camera = camera; job->fraction = fraction;
        job->picture = s.pictures[0];
        PrepareRenderJob(job);
        job->inputs.native_sprites = true;
        /* ROM is copied too: episode/mod changes cannot race a worker read. */
        memcpy(async_renderer.rom, job->inputs.snapshot.rom, async_renderer.rom_size);
        memcpy(async_renderer.ram, job->inputs.ram, async_renderer.ram_size);
        memcpy(async_renderer.previous, job->inputs.previous_ram, async_renderer.ram_size);
        job->inputs.snapshot.rom = async_renderer.rom;
        job->inputs.snapshot.ram = async_renderer.ram;
        job->inputs.ram = async_renderer.ram;
        job->inputs.previous_ram = async_renderer.previous;
        BindAsyncOutputs(job, async_renderer.picture, async_renderer.work);
        async_renderer.generation = s.generation;
        async_renderer.scene_key = scene_key;
        async_renderer.job_epoch = async_renderer.epoch;
        async_renderer.busy = true;
        async_renderer.started = SDL_GetPerformanceCounter();
        s.stats.async_submitted++;
        TraceAsyncCamera("request", camera, s.generation);
        DoomSignalSemaphore(async_renderer.request);
    }
    return true;
}
#else
static void StopAsyncRenderer(void) {}
static void InvalidateAsyncPicture(void) {}
#endif
