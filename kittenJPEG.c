#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <err.h>

/*
This is kittenJPEG, a minimalistic Baseline JPEG-decoder / JPG-file-reader

(c) 2022 by kittennbfive
(c) 2026 by Sasha

AGPLv3+ AND NO WARRANTY!

Please read the fine documentation.

Caution: Depending on the uncommented #define and your input file this tool can create A LOT (hundreds of MB) of text!

version 3 - 20.11.22
*/

#define PRINT_DETAILS_QUANTTABLES
//#define PRINT_DETAILS_BITSTREAM_LOOP_POSITION
//#define PRINT_DETAILS_HUFFMAN_TABLES

const char * comp_names[3]={"Y","Cb","Cr"};

typedef struct
{
	uint_fast8_t H;
	uint_fast8_t V;
	uint_fast8_t Tq;
	uint_fast16_t xi;
	uint_fast16_t yi;
	uint_fast8_t Td; //quant table for DC
	uint_fast8_t Ta; //quant table for AC
} components_data_t;

typedef struct
{
	uint_fast8_t sz;
	uint_fast16_t codeword;
	uint_fast8_t decoded;
} huffman_entry_t;

typedef struct
{
	uint_fast8_t nb_entries;
	huffman_entry_t entries[256];
} huffman_table_t;

typedef uint_fast8_t quantization_table_t[8][8];

typedef struct
{
	double Y;
	double Cb;
	double Cr;
} pixel_YCbCr_t;

typedef struct
{
	uint8_t * data;
	
	uint_fast32_t filesize;
	
	uint_fast32_t pos_in_file;	
	
	uint_fast16_t size_X;
	uint_fast16_t size_Y;
	
	uint_fast8_t Hmax;
	uint_fast8_t Vmax;
	
	uint_fast32_t nb_MCU_total;
	
	uint8_t * compressed_pixeldata;
	
	uint_fast32_t sz_compressed_pixeldata;
	
	uint_fast32_t pos_compressed_pixeldata;
	
	uint_fast32_t bitpos_in_compressed_pixeldata;

	uint_fast8_t nb_components;
	
	components_data_t components_data[4];
	
	huffman_table_t huff_tables[2][2]; //Tc (type=AC/DC), Th (destination identifier)
	
	quantization_table_t quant_tables[4];
	
	pixel_YCbCr_t ** pixels_YCbCr;
	
} picture_t;

typedef double matrix8x8_t[8][8];


uint8_t get1i(uint8_t const * const data, uint_fast32_t * const pos)
{
	uint8_t val=data[*pos];
	(*pos)++;
	return val;
}

uint16_t get2i(uint8_t const * const data, uint_fast32_t * const pos)
{
	uint16_t val=(data[*pos]<<8)|data[(*pos)+1];
	(*pos)+=2;
	return val;
}

uint32_t get4i(uint8_t const * const data, uint_fast32_t * const pos)
{
	uint32_t val=(data[*pos]<<24)|(data[(*pos)+1]<<16)|(data[(*pos)+2]<<8)|(data[(*pos)+3]);
	(*pos)+=4;
	return val;
}

uint16_t get2(uint8_t const * const data, uint_fast32_t * const pos)
{
	return (data[*pos]<<8)|data[(*pos)+1];
}

uint16_t get_marker(uint8_t const * const data, uint_fast32_t * const pos)
{
	return get2i(data, pos);
}


char *to_bin(const uint16_t word, const uint8_t sz)
{
	static char str[17];
	memset(str, '\0', 17);
	
	uint8_t i;
	for(i=0; i<sz; i++)
		str[i]='0'+!!(word&(1<<(sz-i-1)));
	
	return str;
}


uint_fast32_t ceil_to_multiple_of(const uint_fast32_t val, const uint_fast32_t multiple)
{
	return (uint_fast32_t)(multiple*ceil((double)val/multiple));
}


void skip_EXIF(picture_t * const pic)
{
	uint16_t len=get2i(pic->data, &(pic->pos_in_file));
	printf("APP1 (probably EXIF) found (length %u bytes), skipping\n", len);
	pic->pos_in_file+=len-2;	
}

void parse_APP0(picture_t * const pic)
{
	uint16_t len=get2i(pic->data, &(pic->pos_in_file));
	printf("APP0 found (length %u bytes)\n", len);
	if(len<16)
		errx(1, "APP0: too short");
	
	uint8_t identifier[5];
	memcpy(identifier, &pic->data[pic->pos_in_file], 5);
	pic->pos_in_file+=5;
	
	uint_fast8_t version_major=get1i(pic->data, &(pic->pos_in_file));
	uint_fast8_t version_minor=get1i(pic->data, &(pic->pos_in_file));
	uint_fast8_t units=get1i(pic->data, &(pic->pos_in_file));
	uint_fast16_t Xdensity=get2i(pic->data, &(pic->pos_in_file));
	uint_fast16_t Ydensity=get2i(pic->data, &(pic->pos_in_file));
	uint_fast16_t Xthumbnail=get1i(pic->data, &(pic->pos_in_file));
	uint_fast16_t Ythumbnail=get1i(pic->data, &(pic->pos_in_file));
		
	if(memcmp(identifier, "JFIF\x00", 5))
		errx(1, "APP0: invalid identifier");
	
	printf("version %u.%u\n", version_major, version_minor);
	printf("units %u\n", units);
	printf("density X %lu Y %lu\n", Xdensity, Ydensity);
		
	uint_fast32_t bytes_thumbnail=3*Xthumbnail*Ythumbnail;
	
	if(bytes_thumbnail)
	{
		printf("thumbnail %lu bytes, skipping\n", bytes_thumbnail);
		pic->pos_in_file+=bytes_thumbnail;
	}
	else
		printf("no thumbnail\n");
}

void parse_SOF0(picture_t * const pic)
{
	uint16_t len=get2i(pic->data, &(pic->pos_in_file));
	printf("SOF0 found (length %u bytes)\n", len);
	
	uint_fast8_t P=get1i(pic->data, &(pic->pos_in_file));
	uint_fast16_t Y=get2i(pic->data, &(pic->pos_in_file));
	uint_fast16_t X=get2i(pic->data, &(pic->pos_in_file));
	uint_fast8_t Nf=get1i(pic->data, &(pic->pos_in_file));
	
	if(P!=8)
		errx(1, "SOF0: P!=8 unsupported");
	
	if(Y==0)
		errx(1, "SOF0: Y==0 unsupported");
	
	printf("P %u (must be 8)\n", P);
	printf("imagesize X %lu Y %lu\n", X, Y);
	printf("Nf (number of components) %u\n", Nf);
	
	if(Nf!=3)
		errx(1, "picture does not have 3 components, this code will not work");
	
	pic->size_X=X;
	pic->size_Y=Y;
	
	uint_fast8_t i;
	for(i=0; i<Nf; i++)
	{
		uint8_t C=get1i(pic->data, &(pic->pos_in_file));
		uint8_t HV=get1i(pic->data, &(pic->pos_in_file));
		uint8_t H=(HV>>4)&0x0f;
		uint8_t V=HV&0x0f;
		uint8_t Tq=get1i(pic->data, &(pic->pos_in_file));
		
		pic->components_data[i].H=H;
		pic->components_data[i].V=V;
		pic->components_data[i].Tq=Tq;
		
		printf("component %u (%s) C %u, H %u, V %u, Tq %u\n", i, comp_names[i], C, H, V, Tq);
	}
	
	pic->nb_components=Nf;
	
	uint_fast8_t Hmax=0,Vmax=0;

	for(i=0; i<pic->nb_components; i++)
	{
		if(pic->components_data[i].H>Hmax)
			Hmax=pic->components_data[i].H;
		if(pic->components_data[i].V>Vmax)
			Vmax=pic->components_data[i].V;
	}
	
	pic->Hmax=Hmax;
	pic->Vmax=Vmax;
	
	pic->nb_MCU_total=(ceil_to_multiple_of(pic->size_X, 8*Hmax)/(8*Hmax))*(ceil_to_multiple_of(pic->size_Y, 8*Hmax)/(8*Vmax));
	
	printf("Hmax %u Vmax %u\n", Hmax, Vmax);
	printf("MCU_total %lu\n", pic->nb_MCU_total);
	
	uint16_t xi,yi;
	for(i=0; i<pic->nb_components; i++)
	{
		xi=(uint16_t)ceil((double)pic->size_X*pic->components_data[i].H/Hmax);
		yi=(uint16_t)ceil((double)pic->size_Y*pic->components_data[i].V/Vmax);
		
		pic->components_data[i].xi=xi;
		pic->components_data[i].yi=yi;
		
		printf("component %u (%s) xi %u yi %u\n", i, comp_names[i], xi, yi);
	}
	
	printf("allocating memory for pixels\n");
	
	uint_fast16_t x,y;
	pic->pixels_YCbCr=malloc(pic->size_X*sizeof(pixel_YCbCr_t*));
	for(x=0; x<pic->size_X; x++)
		pic->pixels_YCbCr[x]=malloc(pic->size_Y*sizeof(pixel_YCbCr_t));
	
	for(x=0; x<pic->size_X; x++)
	{
		for(y=0; y<pic->size_Y; y++)
		{
			pic->pixels_YCbCr[x][y].Y=0;
			pic->pixels_YCbCr[x][y].Cb=0;
			pic->pixels_YCbCr[x][y].Cr=0;
		}
	}
	printf("memory allocated\n");
}

void parse_DHT(picture_t * const pic)
{
	uint16_t len=get2i(pic->data, &(pic->pos_in_file));
	printf("DHT found (length %u bytes)\n", len);

  uint16_t pos_end = pic->pos_in_file + len - 2;

  while (pic->pos_in_file < pos_end) {
    uint8_t TcTh=get1i(pic->data, &(pic->pos_in_file));
    uint8_t Tc=(TcTh>>4)&0x0f;
    uint8_t Th=TcTh&0x0f;
	
    printf("Tc %u (%s table)\n", Tc, (Tc==0)?"DC":"AC");
    printf("Th (table destination identifier) %u\n", Th);
	
    uint8_t L[16];
    uint8_t mt=0;
    uint8_t i;
    for(i=0; i<16; i++)
    {
      L[i]=get1i(pic->data, &(pic->pos_in_file));
      mt+=L[i];
#ifdef PRINT_DETAILS_HUFFMAN_TABLES
      printf("length %u bits: %u codes\n", i+1, L[i]);
#endif
    }
	
    printf("total %u codes\n", mt);

    uint16_t codeword=0;
	
    for(i=0; i<16; i++)
    {
      uint8_t j;
      for(j=0; j<L[i]; j++)
      {
        int next_index = pic->huff_tables[Tc][Th].nb_entries;
        uint8_t V=get1i(pic->data, &(pic->pos_in_file));
#ifdef PRINT_DETAILS_HUFFMAN_TABLES
        printf("codeword %s (0x%x) (sz %u) -> %u (0x%x)\n", to_bin(codeword, i+1), codeword, i+1, V, V);
#endif
        pic->huff_tables[Tc][Th].entries[next_index].sz=i+1;
        pic->huff_tables[Tc][Th].entries[next_index].codeword=codeword;
        pic->huff_tables[Tc][Th].entries[next_index].decoded=V;
        pic->huff_tables[Tc][Th].nb_entries++;
			
        codeword++;
      }
      codeword<<=1;
    }
  }
}

void parse_SOS(picture_t * const pic)
{
	uint16_t len=get2i(pic->data, &(pic->pos_in_file)); //without actual bitmap data
	printf("SOS found (length %u bytes)\n", len);
	
	uint8_t Ns=get1i(pic->data, &(pic->pos_in_file));
	printf("Ns %u\n", Ns);
	
	uint8_t j;
	for(j=0; j<Ns; j++)
	{
		uint8_t Cs=get1i(pic->data, &(pic->pos_in_file));
		uint8_t TdTa=get1i(pic->data, &(pic->pos_in_file));
		uint8_t Td=(TdTa>>4)&0x0f;
		uint8_t Ta=TdTa&0x0f;
		
		printf("component %u (%s) Cs %u Td %u Ta %u\n", j, comp_names[j], Cs, Td, Ta);
		pic->components_data[j].Td=Td; //DC
		pic->components_data[j].Ta=Ta; //AC
	}
	
	uint8_t Ss=get1i(pic->data, &(pic->pos_in_file));
	uint8_t Se=get1i(pic->data, &(pic->pos_in_file));
	uint8_t AhAl=get1i(pic->data, &(pic->pos_in_file));
	uint8_t Ah=(AhAl>>4)&0x0f;
	uint8_t Al=AhAl&0x0f;
	
	printf("Ss %u Se %u Ah %u Al %u\n", Ss, Se, Ah, Al);
	
	pic->pos_compressed_pixeldata=pic->pos_in_file;
	
	printf("compressed pixeldata starts at pos %lu\n\n", pic->pos_compressed_pixeldata);
}

void parse_DQT(picture_t * const pic)
{
	uint16_t Lq=get2i(pic->data, &(pic->pos_in_file));
	printf("DQT found (length %u bytes)\n", Lq);

  uint16_t pos_end = pic->pos_in_file + Lq - 2;

  while (pic->pos_in_file < pos_end) {
    uint8_t PqTq=get1i(pic->data, &(pic->pos_in_file));
    uint8_t Pq=(PqTq>>4)&0x0f;
    uint8_t Tq=PqTq&0x0f;
    printf("Pq (element precision) %u -> %u bits\n", Pq, (Pq==0)?8:16);
    printf("Tq (table destination identifier) %u\n", Tq);
	
    if(Pq!=0)
      errx(1, "DQT: only 8 bit precision supported");
	
    uint16_t nb_data_bytes=Lq-2-1;
	
    if(nb_data_bytes!=64)
      errx(1, "DQT: nb_data_bytes!=64");

    uint8_t u,v;
    for(u=0; u<8; u++)
    {
      for(v=0; v<8; v++)
      {
        uint8_t Q=get1i(pic->data, &(pic->pos_in_file));
#ifdef PRINT_DETAILS_QUANTTABLES
        printf("%02u ", Q);
#endif
        pic->quant_tables[Tq][u][v]=Q;
      }
#ifdef PRINT_DETAILS_QUANTTABLES
      printf("\n");
#endif
    }
  }
	printf("\n");
}

void open_new_picture(char const * const name, picture_t * const picture)
{
	FILE *f=fopen(name, "rb");
	if(!f)
		err(1, "fopen %s failed", name);
	
	fseek(f, 0, SEEK_END);
	picture->filesize=ftell(f);
	fseek(f, 0, SEEK_SET);
	
	picture->data=malloc(picture->filesize*sizeof(uint8_t));
	if(!picture->data)
		err(1, "malloc for %s failed", name);
		
	if(fread(picture->data, picture->filesize, 1, f)!=1)
		err(1, "fread for %s failed", name);
		
	fclose(f);
	
	printf("%lu bytes read from %s\n\n", picture->filesize, name);
	
	picture->pos_in_file=0;
	
	picture->nb_components=0;
	
	picture->huff_tables[0][0].nb_entries=0;
	picture->huff_tables[0][1].nb_entries=0;
	picture->huff_tables[1][0].nb_entries=0;
	picture->huff_tables[1][1].nb_entries=0;
}

void close_picture(picture_t * const picture)
{
	free(picture->data);
	free(picture->compressed_pixeldata);
	uint_fast16_t x;
	for(x=0; x<picture->size_X; x++)
		free(picture->pixels_YCbCr[x]);
	free(picture->pixels_YCbCr);
	
	printf("cleaned up, all fine\n");	
}

void copy_bitmap_data_remove_stuffing(picture_t * const pic)
{
	printf("removing stuffing...\n");
	
	//get length of bitstream without stuffing
	
	uint_fast32_t pos=pic->pos_compressed_pixeldata;
	uint_fast32_t size=0;
	uint8_t byte;
	uint16_t combined=0;
	
	do
	{
		if(pos>=pic->filesize)
			errx(1, "marker EOI (0xFFD9) missing");
		
		byte=pic->data[pos++];
		if(byte==0xFF)
		{
			uint8_t byte2=pic->data[pos++];
			if(byte2!=0x00)
				combined=(byte<<8)|byte2;
			else
				size++;
		}
		else
			size++;
	} while(combined!=0xFFD9);
	
	uint_fast32_t size_stuffed=pos-pic->pos_compressed_pixeldata-2;
	
	//remove stuffing
	
	pic->compressed_pixeldata=malloc(size*sizeof(uint8_t));
	if(!pic->compressed_pixeldata)
		err(1, "malloc");
	
	printf("%lu bytes with stuffing\n", size_stuffed);
	
	uint_fast32_t i;
	uint_fast32_t size_without_stuffing;
	
	for(i=pic->pos_compressed_pixeldata, pos=0, size_without_stuffing=0; i<(pic->pos_compressed_pixeldata+size_stuffed); )
	{
		if(pic->data[i]!=0xFF)
		{
			pic->compressed_pixeldata[pos++]=pic->data[i++];
			size_without_stuffing++;
		}
		else if(pic->data[i]==0xFF && pic->data[i+1]==0x00)
		{
			pic->compressed_pixeldata[pos++]=0xFF;
			size_without_stuffing++;
			i+=2;
		}
		else
			errx(1, "unexpected marker 0x%02x%02x found in bitstream", pic->data[i], pic->data[i+1]);
	}
	
	pic->bitpos_in_compressed_pixeldata=0;
	pic->sz_compressed_pixeldata=size_without_stuffing;
	pic->pos_in_file=pic->pos_compressed_pixeldata+size_stuffed;
	
	printf("%lu data bytes without stuffing\n\n", size_without_stuffing);
}

#include "pg.h"
#include "do_the_rest.h"
#include "Huffman-kitten.h"
#include "kitten-gl.h"
#include "ppm.h"

void parse_bitmap_data(picture_t * const pic, PGCtx *pg)
{
	printf("parsing bitstream...\n");
	
	uint_fast8_t component=0; //Cs
	uint_fast8_t data_unit;
  uint_fast8_t u,v;
	
	int16_t precedent_DC[4]={0,0,0,0};
	
	uint_fast32_t nb_MCU=0;

	matrix8x8_t matrix;

  // PG ----------------------------
  pg->proper.w = ceil_to_multiple_of (pic->size_X, 16);
  pg->proper.h = ceil_to_multiple_of (pic->size_Y, 16);
  printf ("IMAGE W = %d, H = %d\n",
      pg->proper.w, pg->proper.h);

  pg_set_component_dimensions (pg, 0, pic->components_data[0].xi, pic->components_data[0].yi);
  pg_set_component_dimensions (pg, 1, pic->components_data[0].xi, pic->components_data[0].yi);
  pg_set_component_dimensions (pg, 2, pic->components_data[0].xi, pic->components_data[0].yi);

//  kitten_dump = (float*)malloc (pg->aligned[0].w * pg->aligned[0].h  * sizeof (float));
  
  for (int gg= 0; gg < 3; gg++) {
    int mcu_size = pic->components_data[gg].V*pic->components_data[gg].H;
    int totsize = mcu_size * pic->nb_MCU_total * 64;

    printf ("[%d] MCU SIZE = %d, TOT SIZE = %d, aligned w = %d, aligned h = %d\n",
        gg, mcu_size, totsize, pg->aligned[gg].w, pg->aligned[gg].h);

    pg->data[gg] = malloc (totsize * 2);
    pg->pos[gg] = 0;

    // only 2 quant tables on input, but 3 that we use
    int tq = pic->components_data[gg].Tq;

    PG_COPY_QTABL (pg, gg, pic->quant_tables[tq]);
  }
  // ------------------------------
  
	for(nb_MCU=0; nb_MCU<pic->nb_MCU_total; nb_MCU++)
	{
		for(component=0; component<pic->nb_components; component++)
		{
			for(data_unit=0; data_unit<(pic->components_data[component].V*pic->components_data[component].H); data_unit++)
			{
#ifdef PRINT_DETAILS_BITSTREAM_LOOP_POSITION
				printf("MCU %lu component %u (%s) data_unit %u Td %u Ta %u\n", nb_MCU, component, comp_names[component], data_unit, pic->components_data[component].Td, pic->components_data[component].Ta);
#endif
				for(u=0; u<8; u++)
					for(v=0; v<8; v++)
						matrix[u][v]=0;

        HAFFMANN_2MATREX (matrix, pic, component, precedent_DC);
        MATREX_2PG (pg, matrix, component);
			}
		}
	}
	printf("parsed %lu MCU\n", nb_MCU);
}

void skip_COM (picture_t * const pic)
{
  uint16_t Lq=get2i(pic->data, &(pic->pos_in_file));
	printf("COM found (length %u bytes)\n", Lq);

  pic->pos_in_file+=Lq-2;
}

void parse_picture(picture_t * const picture, PGCtx *pg)
{
	while(picture->pos_in_file<=picture->filesize-2)
	{
		uint16_t marker;
		
		marker=get_marker(picture->data, &(picture->pos_in_file));
		
		switch(marker)
		{
			case 0xFFD8:	printf("SOI found\n"); break;

      case 0xFFFE:  skip_COM (picture); break;
			case 0xFFE1:	skip_EXIF(picture); break;
			
			case 0xFFE0:	parse_APP0(picture); break;
			case 0xFFDB:	parse_DQT(picture); break;
			case 0xFFC0:	parse_SOF0(picture); break;
			case 0xFFC4:	parse_DHT(picture); break;
			case 0xFFDA:	parse_SOS(picture);
							copy_bitmap_data_remove_stuffing(picture);
              parse_bitmap_data(picture, pg);
							break;
			
			case 0xFFD9:	printf("EOI found\n"); break;
			
			default:
        
        errx(1, "unknown marker 0x%04x pos %lu", marker, picture->pos_in_file); break;
		}
		printf("\n");
	}
}

int main(int argc, char **argv)
{
  PGCtx pg;
	picture_t pic;
  char *filename = "kitten_small.jpg";
  
  if (argc > 1)
    filename = argv[1];
	
	open_new_picture(filename, &pic);
	parse_picture(&pic, &pg);

#if 0
  DO_THE_REST (&pic, &pg); // from mcus of symbols to a pic
	write_ppm(&pic, "kitten.ppm");
	close_picture(&pic);
#endif

  kitten_gl_show (&pg, filename);

  printf("exit nicely\n");
	return 0;
}
