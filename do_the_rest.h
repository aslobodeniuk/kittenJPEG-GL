/*
This is kittenJPEG, a minimalistic Baseline JPEG-decoder / JPG-file-reader

(c) 2022 by kittennbfive
(c) 2026 by Sasha

AGPLv3+ AND NO WARRANTY!

Please read the fine documentation.

Caution: Depending on the uncommented #define and your input file this tool can create A LOT (hundreds of MB) of text!

version 3 - 20.11.22
*/

//#define PRINT_DETAILS_BITSTREAM_MATRIX_DEQUANT
//#define PRINT_DETAILS_BITSTREAM_MATRIX_DECODED

void store_data_unit_YCbCr(picture_t * const pic, const uint_fast32_t MCU, const uint_fast8_t component, const uint_fast8_t data_unit, const matrix8x8_t data)
{
	uint_fast8_t zoomX, zoomY;
	
	zoomX=pic->Hmax/pic->components_data[component].H; // 1 for Y, 2 for UV
	zoomY=pic->Vmax/pic->components_data[component].V;
  
	uint_fast8_t scaleX, scaleY;
	
	scaleX=8*pic->components_data[component].H; // 8 * H !!
	scaleY=8*pic->components_data[component].V;
	
	uint_fast16_t startX, startY;
	
	startX=MCU%(ceil_to_multiple_of(pic->size_X, 8*pic->Hmax)/(scaleX*zoomX));
	startY=MCU/(ceil_to_multiple_of(pic->size_X, 8*pic->Hmax)/(scaleY*zoomY)); //yes, size_X and H!
	
	uint_fast16_t startHiX=data_unit%pic->components_data[component].H;
	uint_fast16_t startHiY=data_unit/pic->components_data[component].H; //yes, H!
	
	uint_fast8_t x,y;
	uint_fast8_t zx,zy;
	
	uint_fast32_t posX, posY;
	
	for(x=0; x<8; x++)
	{
		for(y=0; y<8; y++)
		{
			for(zx=0; zx<zoomX; zx++)
			{
				for(zy=0; zy<zoomY; zy++)
				{
					posX=(scaleX*startX+8*startHiX+x)*zoomX+zx;
					posY=(scaleY*startY+8*startHiY+y)*zoomY+zy;
					
					if(posX<pic->size_X && posY<pic->size_Y)
					{
            /* if (x == 0 && y == 0) */
            /*   printf ("component %d [%d][%d]\n", component, posX, posY); */
						switch(component)
						{
							case 0: pic->pixels_YCbCr[posX][posY].Y=data[x][y]; break;
							case 1: pic->pixels_YCbCr[posX][posY].Cb=data[x][y]; break;
							case 2: pic->pixels_YCbCr[posX][posY].Cr=data[x][y]; break;
							default: errx(1, "unknown component"); break;
						}
					}
				}
			}
		}
	}
}


void reverse_ZZ_and_dequant(picture_t const * const pic, const uint8_t quant_table, const matrix8x8_t inp, matrix8x8_t outp)
{
	const uint_fast8_t reverse_ZZ_u[8][8]={	{0, 0, 1, 2, 1, 0, 0, 1 },
											{2, 3, 4, 3, 2, 1, 0, 0 },
											{1, 2, 3, 4, 5, 6, 5, 4 },
											{3, 2, 1, 0, 0, 1, 2, 3 },
											{4, 5, 6, 7, 7, 6, 5, 4 },
											{3, 2, 1, 2, 3, 4, 5, 6 },
											{7, 7, 6, 5, 4, 3, 4, 5 },
											{6, 7, 7, 6, 5, 6, 7, 7 }	};
										
	const uint_fast8_t reverse_ZZ_v[8][8]={	{0, 1, 0, 0, 1, 2, 3, 2 },
											{1, 0, 0, 1, 2, 3, 4, 5 },
											{4, 3, 2, 1, 0, 0, 1, 2 },
											{3, 4, 5, 6, 7, 6, 5, 4 },
											{3, 2, 1, 0, 1, 2, 3, 4 },
											{5, 6, 7, 7, 6, 5, 4, 3 },
											{2, 3, 4, 5, 6, 7, 7, 6 },
											{5, 4, 5, 6, 7, 7, 6, 7 }	};
	
	uint_fast8_t u,v;
	
	for(u=0; u<8; u++)
		for(v=0; v<8; v++)
			outp[reverse_ZZ_u[u][v]][reverse_ZZ_v[u][v]]=inp[u][v]*pic->quant_tables[quant_table][u][v];
}


void data_unit_do_idct(const matrix8x8_t inp, matrix8x8_t outp)
{
	double sxy=0;
	
	uint_fast8_t x, y;
	uint_fast8_t u, v;
	
	const double C[8]={1.0/sqrt(2.0), 1, 1, 1, 1, 1, 1, 1};
	
	for(y=0; y<8; y++)
	{
		for(x=0; x<8; x++)
		{
			sxy=0;
			for(u=0; u<=7; u++) // xk
			{
				for(v=0; v<=7; v++) // yk
				{
					double Svu=inp[v][u]; //index order!
					sxy+=C[u]*C[v]*Svu*cos(((2.0*x+1.0)*u*M_PI)/16.0)*cos(((2.0*y+1.0)*v*M_PI)/16.0);
				}
			}
			
			sxy*=0.25;
			sxy+=128;
			outp[x][y]=sxy;
		}
	}
}

void DO_THE_REST (picture_t * const pic, PGCtx *pg)
{
  uint_fast8_t data_unit;
  uint_fast32_t nb_MCU=0;
  uint_fast8_t component=0; //Cs
  int u, v;

  for (int gg= 0; gg < 3; gg++)
    pg->pos[gg] = 0;
  
  for(nb_MCU=0; nb_MCU<pic->nb_MCU_total; nb_MCU++)
	{ 
		for(component=0; component<pic->nb_components; component++)
		{
      for(data_unit=0; data_unit<(pic->components_data[component].V*pic->components_data[component].H); data_unit++)
			{
        matrix8x8_t matrix;
        PG_2MATREX (matrix, pg, component);
        
        matrix8x8_t matrix_dequant;
				
        reverse_ZZ_and_dequant(pic, pic->components_data[component].Tq, matrix, matrix_dequant);

        matrix8x8_t matrix_decoded;
				
        data_unit_do_idct(matrix_dequant, matrix_decoded);
        store_data_unit_YCbCr(pic, nb_MCU, component, data_unit, matrix_decoded);
      }
    }
  }
}
