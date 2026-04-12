// Build (example):
//   cc -O3 turbo.c -o kit -ljpeg -lX11 -lEGL -lGLESv2 -lm
//
// On some systems the library is -lturbojpeg (TurboJPEG API) and/or -ljpeg (libjpeg-turbo).
// This code uses the libjpeg API, typically provided by libjpeg-turbo as "libjpeg".

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <jpeglib.h>
#include "../pg-window.h"

static void die(const char *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(1);
}

typedef struct
{
  struct jpeg_decompress_struct c;
  jvirt_barray_ptr *coef_arrays;

  struct {
    int w, h;
  } aligned [3], proper;

  float qtable[3][64];
} JPEGGLCtx;

void pg_upload_from_libjpeg (GLuint tex, JPEGGLCtx *ctx, int comp)
{  
  const jpeg_component_info *compptr = &ctx->c.comp_info[comp];

  int blocks_w = compptr->width_in_blocks;
  int blocks_h = compptr->height_in_blocks;
  
  printf("component %d: blocks %dx%d, quant_tbl_no=%d\n",
      comp, blocks_w, blocks_h, compptr->quant_tbl_no);

  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
  
  for (int by = 0; by < blocks_h; by++) {
    JBLOCKARRAY row = (ctx->c.mem->access_virt_barray)(
      (j_common_ptr)&ctx->c, ctx->coef_arrays[comp], by, 1, FALSE);

    // lijpeg has 64 rows of 4096 values
    // we want 512 rows of 512 values

    // so one row of libjpeg has 8 rows of texture
    // upload 8 lines then
    glTexSubImage2D(GL_TEXTURE_2D, 0,       
        0,
        by * 8, // at 0, [0 .. 512]

        blocks_w * 8, // 512 vals of x
        8,
        GL_RED_INTEGER, GL_SHORT, (const void*)&row[0][0]);
  }
}

#include "turbo-gl.h"

static void
_turbo_init_source (j_decompress_ptr cinfo)
{
  printf ("init_source\n");
}

static boolean
_fill_input_buffer (j_decompress_ptr cinfo)
{
  /* Assume error */
  return FALSE;
}

static void
_skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
  JPEGGLCtx *ctx = (JPEGGLCtx *)cinfo;

  printf ("skip %ld bytes\n", num_bytes);

  if (num_bytes > 0 && cinfo->src->bytes_in_buffer >= num_bytes) {
    cinfo->src->next_input_byte += (size_t) num_bytes;
    cinfo->src->bytes_in_buffer -= (size_t) num_bytes;
  }
}

static boolean
_resync_to_restart (j_decompress_ptr cinfo, int desired)
{
  printf ("resync_to_start (%d)\n", desired);
  return TRUE;
}

static void
_term_source (j_decompress_ptr cinfo)
{
  printf ("term_source\n");
  return;
}

uint8_t * _read_entire_file (const char *filename, long *size)
{
  FILE *fp = fopen(filename, "rb");

  if (NULL == fp)
    return NULL;
  
  fseek(fp, 0, SEEK_END);
  *size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  uint8_t *ret = malloc(*size);
  if (1 != fread(ret, *size, 1, fp)) {
    free (ret);
    ret = NULL;
  }

  fclose(fp);
  return ret;
}

int main(int argc, char **argv)
{
  uint8_t *data;
  long size = 0;
  
  // ------------ FILENAME AND ARGS
  if (argc != 2) die("usage: jpeg_extract_dct_coeffs <input.jpg>");
  const char *filename = argv[1];

  data = _read_entire_file (filename, &size);  
  if (!data) die("failed to open input file");
  // ------------------------------
  
  JPEGGLCtx ctx;
  struct jpeg_error_mgr jerr = {0};
  ctx.c.err = jpeg_std_error(&jerr);
  struct jpeg_source_mgr jsrc =
  {
    .init_source = _turbo_init_source,
    .fill_input_buffer = _fill_input_buffer,
    .skip_input_data = _skip_input_data,
    .resync_to_restart = _resync_to_restart,
    .term_source = _term_source
  };
  
  // ----------- INIT CPU DEC
  jpeg_create_decompress(&ctx.c);

  ctx.c.src = &jsrc;
  // ------------------------

  // ------------------------ PARSE & DEC CPU
  {
    uint64_t start = pg_perf_start ();

    ctx.c.src->next_input_byte = data;
    ctx.c.src->bytes_in_buffer = size;
    
    if (jpeg_read_header(&ctx.c, TRUE) != JPEG_HEADER_OK)
      die("jpeg_read_header failed");

    ctx.coef_arrays = jpeg_read_coefficients(&ctx.c);
    if (!ctx.coef_arrays) die("jpeg_read_coefficients failed");
    pg_perf_end (start, "turbo parse");
  }

  printf("image: %ux%u, components=%d\n",
      ctx.c.image_width, ctx.c.image_height, ctx.c.num_components);

  for (int comp = 0; comp < ctx.c.num_components; comp++) {
    const jpeg_component_info *compptr = &ctx.c.comp_info[comp];
    
    ctx.aligned[comp].w = compptr->width_in_blocks * 8;
    ctx.aligned[comp].h = compptr->height_in_blocks * 8;

    const JQUANT_TBL *qt = ctx.c.quant_tbl_ptrs[ctx.c.comp_info[comp].quant_tbl_no];
    for(int u=0; u<8; u++) {
      for(int v=0; v<8; v++) {
        ctx.qtable[comp][u * 8 + v] = qt->quantval[u * 8 + v];
      }
    }
  }
  ctx.proper.w = ctx.c.image_width;
  ctx.proper.h = ctx.c.image_height;
  // ----------------------------------------

  kitten_gl_show (&ctx, filename);

  // ----------- CLOSE DEC
  jpeg_finish_decompress(&ctx.c);
  jpeg_destroy_decompress(&ctx.c);
  // -----------

  free(data);
  return 0;
}
