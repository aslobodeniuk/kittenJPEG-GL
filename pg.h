/* Adaptation Layer */

typedef struct {
  float *data[3];
  int pos[3];
  struct {
    int w, h;
  } aligned [3];

  float qtable[3][64];
} PGCtx;

static void PG_2MATREX (matrix8x8_t matrix, PGCtx *pg, int component) {
  int u, v;
  for(u=0; u<8; u++)
    for(v=0; v<8; v++) {
      matrix[u][v] = pg->data[component][pg->pos[component] + u * 8 + v];
    }
      
  pg->pos[component] += 8*8;  
}

static void MATREX_2PG (PGCtx *pg, matrix8x8_t matrix, int component) {
  int u, v;
  for(u=0; u<8; u++)
    for(v=0; v<8; v++) {
      pg->data[component][pg->pos[component] + u * 8 + v] = matrix[u][v];
    }
  pg->pos[component] += 8*8;  
}

static void PG_COPY_QTABL (PGCtx *pg, int component, quantization_table_t qt)
{
  int u, v;
  const int zigzag8x8[64] = {
    0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
  };
  
  for(u=0; u<8; u++)
  {
    for(v=0; v<8; v++)
    {
      pg->qtable[component][zigzag8x8[u * 8 + v]] = qt[u][v];
    }
  }
}

unsigned int
pg_bytes_hash (const void* data, int size)
{
  const signed char *p, *e;
  unsigned int h = 5381;

  for (p = (signed char *)data, e = (signed char *)data + size; p != e; p++)
    h = (h << 5) + h + *p;

  return h;
}

int kitten_pos;
float *kitten_dump;

