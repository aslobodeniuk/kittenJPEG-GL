/* OpenGL JPEG FUN
   (c) 2026 Sasha
   AGPLv3+ AND NO WARRANTY!
   Version 69
*/

// Compile with: gcc kittenJPEG.c -lX11 -lEGL -lGLESv2 -lm -o kit

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "pg-window.h"

typedef enum {
  PG_SHADER_VERTEX_PASSTHROUGH,
  PG_SHADER_FRAGMENT_PASSTHROUGH,
  PG_SHADER_ZZ_TO_DCT,
  PG_SHADER_DCT_TO_P,
  PG_SHADER_YUV_TO_RGB
} PGShader;

static struct {
  GLuint sh;
  GLenum type;
  const char *code;
  GLenum output_format;
} PGShaderBase[] = {
  [PG_SHADER_VERTEX_PASSTHROUGH] = {
    .type = GL_VERTEX_SHADER,
    .code =
    "#version 300 es\n"
    "layout (location = 0) in vec2 pos;\n"
    "layout (location = 1) in vec2 tex;\n"
    "out vec2 texCoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "    texCoord = tex;\n"
    "}"
  },
  [PG_SHADER_FRAGMENT_PASSTHROUGH] = {
    .type = GL_FRAGMENT_SHADER,
    .code =
    "#version 300 es\n"
    "precision lowp float;\n"
    "precision lowp int;\n"
    "in vec2 texCoord;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D rgbTex;\n"
    "void main() {\n"
    "  fragColor = texture(rgbTex, texCoord);\n"
    "}\n"
  },
  [PG_SHADER_ZZ_TO_DCT] = {
    .type = GL_FRAGMENT_SHADER,
    .output_format = GL_R16F,
    .code =
    "#version 300 es\n"
    "precision lowp float;\n"
    "precision lowp int;\n"
    "in vec2 texCoord;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D zigzagInpP;\n"
    "uniform float qTable[64];\n"

    "const int zigzag8x8[64] = int[64](\n"
    "  0,  1,  5,  6, 14, 15, 27, 28,\n"
    "  2,  4,  7, 13, 16, 26, 29, 42,\n"
    "  3,  8, 12, 17, 25, 30, 41, 43,\n"
    "  9, 11, 18, 24, 31, 40, 44, 53,\n"
    "  10, 19, 23, 32, 39, 45, 52, 54,\n"
    "  20, 22, 33, 38, 46, 51, 55, 60,\n"
    "  21, 34, 37, 47, 50, 56, 59, 61,\n"
    "  35, 36, 48, 49, 57, 58, 62, 63\n"
    ");\n"
    
    "void main() {\n"
    "  ivec2 outPixel = ivec2(gl_FragCoord.xy);"
    // oook, so we need to fetch now a pixel of the same block,
    // but a different one inside of the block.
    // We only play on offset over texCoord.x
    // texCoord.y will be the same.
    // zzj represents position inside the block
    "  int zzj = outPixel.x % 64;\n"
    "  ivec2 pos = ivec2("
    "    outPixel.x - zzj + zigzag8x8[zzj],"
    "    outPixel.y"
    "  );\n"
    "  float dequant = qTable[zzj];\n"
    "  float pixel = texelFetch(zigzagInpP, pos, 0).r;\n"
    "  pixel *= dequant;\n"
    "  fragColor = vec4(pixel, 0.0, 0.0, 1.0);\n"    
    "}\n"
  },
  [PG_SHADER_DCT_TO_P] = {
    .type = GL_FRAGMENT_SHADER,
    .output_format = GL_R16F,
    .code = "#version 300 es\n"
    "precision lowp float;\n"
    "precision lowp int;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D dctInpP;\n"
    "const float M_PI = 3.14159265358979323846;\n"
    // IDCT funcs
    "float idct_sum (float coeff, int x, int y, int xk, int yk)\n"
    "{\n"
    "  float ck = (xk == 0) ? (1.0f / sqrt(2.0f)) : 1.0f;\n"
    "  float cl = (yk == 0) ? (1.0f / sqrt(2.0f)) : 1.0f;\n"
    "  return ck * cl * coeff *  \n"
    "      cos((M_PI * (2.0f * float(x) + 1.0f) * float(yk)) / (2.0f * 8.0f)) *  \n"
    "      cos((M_PI * (2.0f * float (y) + 1.0f) * float(xk)) / (2.0f * 8.0f));\n"
    "}\n"

    "float idct (int input_block_x, int input_y, int x, int y)\n"
    "{\n"
    "  float result = 0.0f\n;"
    "  for (int yk = 0; yk < 8; yk++) {\n"
    "    for (int xk = 0; xk < 8; xk++) {\n"
    "      int xxx = input_block_x + yk*8 + xk;\n"
    "      float coeff = texelFetch(dctInpP, ivec2(xxx, input_y), 0).r;\n"
    "      result += idct_sum (coeff, x, y, xk, yk);\n"
    "    }\n"
    "  }\n"
    "  return result * 0.25f + 128.0f;\n"
    "}\n"

    "void main() {\n"
    // output position
    "    ivec2 outPixel = ivec2(gl_FragCoord.xy);"
    "    int x = outPixel.x % 8;\n"
    "    int y = (outPixel.x % 64) / 8;\n"
    "    int input_block_x = (outPixel.x / 64) * 64;\n"
    "    int input_y = outPixel.y;\n"
    // do idct
    "    float pix = idct (input_block_x, input_y, x, y);\n"
    "    fragColor = vec4(pix, 0.0, 0.0, 1.0);\n"
    "}\n"
  },
  [PG_SHADER_YUV_TO_RGB] = {
    .type = GL_FRAGMENT_SHADER,
    .output_format = GL_RGB,
    .code = "#version 300 es\n"
    "precision lowp float;\n"
    "precision lowp int;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D yuvInpY;\n"
    "uniform sampler2D yuvInpU;\n"
    "uniform sampler2D yuvInpV;\n"
    "uniform int       outWidth;\n"
    "uniform int       outHeight;\n"
    "    vec4 yuv_to_rgb (float y, float u, float v)\n"
    "    {\n"
    "      float r = y + 1.402 * v;\n"
    "      float g = y - 0.344136 * u - 0.714136 * v;\n"
    "      float b = y + 1.772 * u;\n"
    "      return clamp (vec4 (r, g, b, 255.0) / 255.0, 0.0, 1.0);\n"  
    "    }\n"

    "    ivec2 remap_coords (int x, int y, int in_width, int out_width)\n"
    "    {\n"
    "        int out_bpl = out_width / 8;\n"
    "        int block_id = (y / 8) * out_bpl + x / 8;\n"
    "        int in_bpl = in_width / 64;\n"
    "        int in_by = block_id / in_bpl;\n"
    "        int in_bx = (block_id % in_bpl) * 64;\n"
    "        int in_y = in_by;\n"

    "        int in_inbx =  (y % 8) + 8 * ((x % 8));\n"
    "        int in_x = in_bx + in_inbx;\n"
    "        return ivec2(in_x, in_y);\n"
    "    }\n"

    "    ivec2 remap_coordsY (int x, int y, int in_width, int out_width)\n"
    "    {\n"
    "        int out_bpl = out_width / 8;\n"

    // COP ----
    "        int mcu_per_row = out_width / 16;\n"
    "        int mcu_x = x / 16;\n"
    "        int mcu_y = y / 16;\n"
    "        int mcu_id = mcu_y * mcu_per_row + mcu_x;\n"
    "        int sub_x = (x / 8) & 1;\n"
    "        int sub_y = (y / 8) & 1;\n"
    "        int y_sub_id = sub_y * 2 + sub_x;\n"
    "        int block_id = mcu_id * 4 + y_sub_id;\n"
    // -------
    "        int in_bpl = in_width / 64;\n"
    "        int in_by = block_id / in_bpl;\n"
    "        int in_bx = (block_id % in_bpl) * 64;\n"
    "        int in_y = in_by;\n"

    "        int in_inbx =  (y % 8) + 8 * ((x % 8));\n"
    "        int in_x = in_bx + in_inbx;\n"
    "        return ivec2(in_x, in_y);\n"
    "    }\n"
    
    "void main() {\n"
    "    int width = textureSize (yuvInpY, 0).x;\n"
    "    int height = textureSize (yuvInpY, 0).y;\n"
    
    "    ivec2 outPixel = ivec2(gl_FragCoord.xy);\n"
    "    ivec2 inPixelY = remap_coordsY (outPixel.x, (outHeight - outPixel.y - 1), width, outWidth);\n"
    "    ivec2 inPixelCr = remap_coords (outPixel.x / 2, (outHeight - outPixel.y - 1) / 2, width / 2, outWidth / 2);\n"
    "    ivec2 inPixelCb = remap_coords (outPixel.x / 2, (outHeight - outPixel.y - 1) / 2, width / 2, outWidth / 2);\n"
    "    float yy = texelFetch(yuvInpY, inPixelY, 0).r;\n"
    // fixme: add uniforms for uH uV and vH vV
    "    float u = texelFetch(yuvInpU, ivec2(inPixelCb.x, inPixelCb.y), 0).r;\n"
    "    float v = texelFetch(yuvInpV, ivec2(inPixelCr.x, inPixelCr.y), 0).r;\n"
    "    fragColor = yuv_to_rgb (yy, u - 128.0, v - 128.0);\n"
    "}\n",
  }
};

enum {
  PG_INT = -1,
  PG_TEXTURE = 0
};

typedef struct {
  const char *name;
  uint64_t thing;
  int amount; // -1 == int, 0 == texture, > 0 == array
} RFUniform;

void pg_program_link_error (GLuint prog)
{
  GLint logLength;
  glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLength);
  char *log = (char *)malloc(logLength);
  glGetProgramInfoLog(prog, logLength, NULL, log);
  printf("Program failed:\n%s\n", log);
  free(log);

  // FIXME
  exit (1);
}

void rf_shader_error (GLuint shader)
{
  GLint logLength;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

  // Allocate space for the log and retrieve it
  char *log = (char *)malloc(logLength);
  glGetShaderInfoLog(shader, logLength, NULL, log);

  // Print the log
  printf("Shader compile error:\n%s\n", log);

  free(log);

  // FIXME
  exit (1);
}

GLuint pg_compile_shader(GLenum type, const char* src)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);

  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    rf_shader_error (shader);
  }
    
  return shader;
}

GLuint rf_gen_target_buffer ()
{
  static const float quad[] = {
    // pos      // tex
    -1, -1,     0, 0,
    1, -1,     1, 0,
    1,  1,     1, 1,
    -1,  1,     0, 1
  };
  static const unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

  GLuint vao, vbo, ebo;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);
  glBindVertexArray(vao);

  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
  glEnableVertexAttribArray(1);

  return vao;
}


#define PG_N_ELEMENTS(x) (sizeof(x) / sizeof(x[0]))

// FIXME: handle compilation error ok
int pg_compile_all_shaders (void)
{
  for (int i = 0; i < PG_N_ELEMENTS (PGShaderBase); i++) {
    PGShaderBase[i].sh = pg_compile_shader (PGShaderBase[i].type, PGShaderBase[i].code);
  }
}

GLuint pg_create_shader_program (PGShader fragment, RFUniform *unis)
{
  GLuint shader = glCreateProgram();
  glAttachShader(shader, PGShaderBase[PG_SHADER_VERTEX_PASSTHROUGH].sh); // vertex shader always pthrough
  glAttachShader(shader, PGShaderBase[fragment].sh);
  glLinkProgram(shader);

  GLint linkStatus;
  glGetProgramiv(shader, GL_LINK_STATUS, &linkStatus);
  if (!linkStatus) {
    pg_program_link_error (shader);
  }

  glUseProgram(shader);
  int t = 0;
  for (int i = 0; unis[i].name != NULL; i++) {
    GLint location = glGetUniformLocation(shader, unis[i].name);
    if (unis[i].amount == PG_INT) {
      glUniform1i(location, (int)unis[i].thing);
    } else if (unis[i].amount == PG_TEXTURE) {
      glUniform1i(location, t++);
    } else {
      glUniform1fv(location, unis[i].amount, (float*)unis[i].thing);
    }
  }

  return shader;
}

void rf_use_shader_program (GLenum tt, GLuint shader, RFUniform *unis)
{
  glUseProgram(shader);
  int t = 0;
  for (int i = 0; unis[i].name != NULL; i++) {
    /* if amount != 1 it's not a texture but an array */
    if (unis[i].amount != 0)
      continue;
    
    glActiveTexture(GL_TEXTURE0 + t);
    glBindTexture(tt, unis[i].thing);
    t++;
  }  
}

void rf_draw_to_target_buffer (GLuint vao)
{
  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

typedef struct {
  GLuint framebuffer, texture;
} RFFb;

RFFb rf_make_framebuffer (GLenum format, int width, int height)
{
  RFFb ret;

  GLenum err;  
  
  glGenFramebuffers(1, &ret.framebuffer);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    printf ("incomplete\n");
    exit (1);
  }

  glGenTextures(1, &ret.texture);
  glBindFramebuffer(GL_FRAMEBUFFER, ret.framebuffer);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, ret.texture);
  
  glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0,
      format == GL_RGB ? GL_RGB : GL_RED,
      format == GL_RGB ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT, NULL);
  
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER,
      GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ret.texture, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  
  return ret;
}

GLuint rf_create_texture(const float* data, int width, int height) {
    GLuint tex;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // upload
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, data);
    GLint maxTexSize;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    printf("Max texture size: %d\n", maxTexSize);
    
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
      printf("OpenGL error: %x\n", err);
      exit (1);
    }

    // unbind
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

typedef struct {
  struct {
    int w, h;
    PGShader shader;
    RFUniform unis[16];
    int is_window;
  } init;

  RFFb framebuffer;
  GLuint shader_prog;   
} PGDrawStep;

static int
pg_draw_step_compile (PGDrawStep * ds) {
  ds->shader_prog = pg_create_shader_program (ds->init.shader, ds->init.unis);

  if (ds->init.is_window == 0) {
    ds->framebuffer = rf_make_framebuffer (PGShaderBase[ds->init.shader].output_format,
        ds->init.w, ds->init.h);
  }

  return 0;
}

static int
pg_draw_step_draw (PGDrawStep * ds, GLuint vao) {
  glBindFramebuffer(GL_FRAMEBUFFER, ds->framebuffer.framebuffer);
  glViewport(0, 0, ds->init.w, ds->init.h);
  glClear(GL_COLOR_BUFFER_BIT);
  rf_use_shader_program (GL_TEXTURE_2D, ds->shader_prog, ds->init.unis);
  rf_draw_to_target_buffer (vao);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static int
pg_init_jpegdec ()
{
  printf ("GL: %s\n", glGetString(GL_VERSION));

  // TODO: don't require window,
  // get headless context from the display.
  // But then we mast be able to attach to windows context too...
  pg_compile_all_shaders ();  
}

static int
pg_jpegdec_open (PGCtx *pg) // Not really an open, also uploads for now
{
  
}

#define KITTEN_WINDOW_TITLE "JPEG decoder shader : "

void kitten_gl_show (PGCtx *pg, const char* filename) {
  PGX11Window pgw;
  char title[256] = KITTEN_WINDOW_TITLE;
  strncat (title, filename, sizeof(title) - sizeof (KITTEN_WINDOW_TITLE) - 1);
  
  // Split to things?
  pg_window_open_x11 (&pgw, 1024, 1024, title);
  pg_window_bind_context_egl (&pgw);
  
  pg_init_jpegdec ();

  GLuint zigzagInpY = rf_create_texture(pg->data[0], pg->aligned[0].w, pg->aligned[0].h);
  GLuint zigzagInpU = rf_create_texture(pg->data[1], pg->aligned[1].w, pg->aligned[1].h);
  GLuint zigzagInpV = rf_create_texture(pg->data[2], pg->aligned[2].w, pg->aligned[2].h);

  PGDrawStep zigzag_to_dct_y = {
    .init = {
      .w = pg->aligned[0].w,
      .h = pg->aligned[0].h,
      .shader = PG_SHADER_ZZ_TO_DCT,
      .unis = {
        { "zigzagInpP", zigzagInpY, PG_TEXTURE },
        { "qTable", (uint64_t)&pg->qtable[0][0], 64 },
        { NULL }
      }
    }};

  PGDrawStep zigzag_to_dct_u = {
    .init = {
      .w = pg->aligned[1].w,
      .h = pg->aligned[1].h,
      .shader = PG_SHADER_ZZ_TO_DCT,
      .unis = {
        { "zigzagInpP", zigzagInpU, PG_TEXTURE },
        { "qTable", (uint64_t)&pg->qtable[1][0], 64 },
        { NULL }
      }
    }};

  PGDrawStep zigzag_to_dct_v = {
    .init = {
      .w = pg->aligned[2].w,
      .h = pg->aligned[2].h,
      .shader = PG_SHADER_ZZ_TO_DCT,
      .unis = {
        { "zigzagInpP", zigzagInpV, PG_TEXTURE },
        { "qTable", (uint64_t)&pg->qtable[2][0], 64 },
        { NULL }
      }
    }};

  /* The redundancy is that we could create 3 programs of 1 shader.
   * TODO: reuse programs of the same shader (take is_window into account) */
  pg_draw_step_compile (&zigzag_to_dct_y);
  pg_draw_step_compile (&zigzag_to_dct_u);
  pg_draw_step_compile (&zigzag_to_dct_v);

  PGDrawStep dct_to_y = {
    .init = {
      .w = pg->aligned[0].w,
      .h = pg->aligned[0].h,
      .shader = PG_SHADER_DCT_TO_P,
      .unis = {
        { "dctInpP", zigzag_to_dct_y.framebuffer.texture, PG_TEXTURE },
        { NULL }
      }
    }};

  PGDrawStep dct_to_u = {
    .init = {
      .w = pg->aligned[1].w,
      .h = pg->aligned[1].h,
      .shader = PG_SHADER_DCT_TO_P,
      .unis = {
        { "dctInpP", zigzag_to_dct_u.framebuffer.texture, PG_TEXTURE },
        { NULL }
      }
    }};

  PGDrawStep dct_to_v = {
    .init = {
      .w = pg->aligned[2].w,
      .h = pg->aligned[2].h,
      .shader = PG_SHADER_DCT_TO_P,
      .unis = {
        { "dctInpP", zigzag_to_dct_v.framebuffer.texture, PG_TEXTURE },
        { NULL }
      }
    }};

  pg_draw_step_compile (&dct_to_y);
  pg_draw_step_compile (&dct_to_u);
  pg_draw_step_compile (&dct_to_v);
  
  PGDrawStep yuv_to_rgb = {
    .init = {
      .w = pg->proper.w,
      .h = pg->proper.h,
      .shader = PG_SHADER_YUV_TO_RGB,
      .unis = {
        { "yuvInpY", dct_to_y.framebuffer.texture, PG_TEXTURE },
        { "yuvInpU", dct_to_u.framebuffer.texture, PG_TEXTURE },
        { "yuvInpV", dct_to_v.framebuffer.texture, PG_TEXTURE },
        { "outWidth", pg->proper.w, PG_INT },
        { "outHeight", pg->proper.h, PG_INT },
        { NULL }
      }
    }};

  pg_draw_step_compile (&yuv_to_rgb);

  PGDrawStep rgb_to_window = {
    .init = {
      .is_window = 1,
      .w = 1024, // connect to window creation??
      .h = 1024,
      .shader = PG_SHADER_FRAGMENT_PASSTHROUGH,
      .unis = {
        { "rgbTex", yuv_to_rgb.framebuffer.texture, PG_TEXTURE },
        { NULL }
      }
    }};

  pg_draw_step_compile (&rgb_to_window);

  GLuint vao = rf_gen_target_buffer ();
  PGDrawStep * drawen [] = {
    // phase zz --> dct
    &zigzag_to_dct_y,
    &zigzag_to_dct_u,
    &zigzag_to_dct_v,
    // phase dct --> yuv
    &dct_to_y,
    &dct_to_u,
    &dct_to_v,
    // phase (y, u, v) --> rgb
    &yuv_to_rgb,
    // render to the window
    &rgb_to_window,
    // hapi
    NULL
  };

  while (pg_window_loop (&pgw)) {
    for (int i = 0; drawen[i] != NULL; i++)
      pg_draw_step_draw (drawen[i], vao);
    
    pg_window_swap_buffers (&pgw);
  }

  pg_window_close_x11 (&pgw);
}
