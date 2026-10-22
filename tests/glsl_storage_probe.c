/* Real-driver probe: run manually on a desktop with OpenGL 3.3. Verifies
 * allocation counts and pixel readback across repeated presents and resizes. */
#include "gl_core_3_1.h"
#include "glsl_shader.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Config g_config;
void Die(const char *message)
{
    fputs(message, stderr);
    exit(1);
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static GLuint tracked_texture;
static unsigned allocations;
static GLint last_format;
static void (CODEGEN_FUNCPTR *real_image)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void CODEGEN_FUNCPTR CountImage(GLenum target, GLint level, GLint format,
    GLsizei width, GLsizei height, GLint border, GLenum source, GLenum type, const void *pixels)
{
    GLint texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    if ((GLuint)texture == tracked_texture) { allocations++; last_format = format; }
    real_image(target, level, format, width, height, border, source, type, pixels);
}
static GLuint Compile(GLenum kind, const char *source)
{
    GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    CHECK(ok);
    return shader;
}
int main(void)
{
#if SNESRECOMP_SDL3
    CHECK(SDL_Init(SDL_INIT_VIDEO));
#else
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window *window = snesrecomp_sdl_create_window("GLSL storage probe", 32, 32,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    CHECK(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    CHECK(context);
    ogl_LoadFunctions();
    CHECK(ogl_IsVersionGEQ(3, 3));
    printf("Driver: %s / %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER));
    real_image = glTexImage2D;
    glTexImage2D = CountImage;
    GLuint vertex = Compile(GL_VERTEX_SHADER,
        "#version 330 core\n out vec2 uv; void main() {"
        "vec2 p = vec2((gl_VertexID & 1) * 2 - 1, (gl_VertexID >> 1) * 2 - 1);"
        "gl_Position = vec4(p,0,1); uv = (p+1)*0.5; }");
    GLuint fragment = Compile(GL_FRAGMENT_SHADER,
        "#version 330 core\n in vec2 uv; uniform sampler2D Texture; out vec4 color;"
        "void main() { color=texture(Texture,uv); }");
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex); glAttachShader(program, fragment); glLinkProgram(program);
    GLint ok; glGetProgramiv(program, GL_LINK_STATUS, &ok); CHECK(ok);
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    for (unsigned format = 0; format < 3; format++) {
        GlslPass pass[3] = {0};
        GlslShader shader = {0}; shader.n_pass = 2; shader.pass = pass;
        for (unsigned i = 1; i < 3; i++) {
            pass[i].gl_program = program; pass[i].scale_x = pass[i].scale_y = 1;
            pass[i].scale_type_x = pass[i].scale_type_y = GLSL_SOURCE;
            pass[i].wrap_mode = GL_CLAMP_TO_EDGE;
            memset(&pass[i].unif, 0xff, sizeof(pass[i].unif));
            pass[i].unif.Top.Texture = glGetUniformLocation(program, "Texture");
        }
        pass[1].float_framebuffer = format == 1;
        pass[1].srgb_framebuffer = format == 2;
        glGenTextures(1, &pass[1].gl_texture);
        glGenFramebuffers(1, &pass[1].gl_fbo);
        tracked_texture = pass[1].gl_texture; allocations = 0;
        GlTextureWithSize input = {0}; glGenTextures(1, &input.gl_texture);
        for (unsigned frame = 0; frame < 125; frame++) {
            input.width = input.height = frame >= 120 ? 8 : 4;
            unsigned char pixels[8 * 8 * 4];
            for (unsigned i = 0; i < sizeof(pixels); i += 4) {
                pixels[i] = (unsigned char)(frame + 64);
                pixels[i+1] = 80; pixels[i+2] = 160; pixels[i+3] = 255;
            }
            glBindTexture(GL_TEXTURE_2D, input.gl_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, input.width, input.height, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            if (frame == 122)
                pass[1].scale_type_x = pass[1].scale_type_y = GLSL_VIEWPORT;
            unsigned viewport = frame >= 121 ? 24 : 16;
            GlslShader_Render(&shader, &input, 0, 0, viewport, viewport);
            unsigned char readback[4];
            glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, readback);
            for (unsigned channel = 0; channel < 3; channel++)
                CHECK(abs((int)readback[channel] - pixels[channel]) <= (format == 2 ? 1 : 0));
            CHECK(allocations == (frame < 120 ? 1u : frame < 122 ? 2u : 3u));
            CHECK(glGetError() == GL_NO_ERROR);
        }
        CHECK(last_format == (format == 2 ? GL_SRGB8_ALPHA8 : format == 1 ? GL_RGBA32F : GL_RGBA));
        glDeleteTextures(1, &input.gl_texture);
        glDeleteTextures(1, &pass[1].gl_texture);
        glDeleteFramebuffers(1, &pass[1].gl_fbo);
    }
    glTexImage2D = real_image;
    glDeleteVertexArrays(1, &vao); glDeleteProgram(program);
    glDeleteShader(vertex); glDeleteShader(fragment);
#if SNESRECOMP_SDL3
    SDL_GL_DestroyContext(context);
#else
    SDL_GL_DeleteContext(context);
#endif
    SDL_DestroyWindow(window); SDL_Quit();
    puts("GLSL storage: repeated frames, resizes, RGBA/float/sRGB readback passed");
    return 0;
}
