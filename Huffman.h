/*
This is kittenJPEG, a minimalistic Baseline JPEG-decoder / JPG-file-reader

(c) 2022 by kittennbfive
(c) 2026 Cheech Marin
(c) 2026 Tommy Chong

AGPLv3+ AND NO WARRANTY!

Please read the fine documentation.

Caution: Depending on the uncommented #define and your input file this tool can create A LOT (hundreds of MB) of text!

version 3 - 20.11.22
*/

//#define PRINT_DETAILS_HUFFMAN_TABLES

int16_t convert_to_neg(uint16_t bits, const uint8_t sz)
{
	int16_t ret=-((bits^0xFFFF)&((1<<sz)-1));
	return ret;
}

uint16_t bitstream_get_bits(picture_t * const pic, const uint_fast8_t nb_bits)
{
	if(nb_bits>16)
		errx(1, "bitstream_get_bits: >16 bits requested");
	
	uint_fast32_t index=pic->bitpos_in_compressed_pixeldata/8;
	int_fast8_t pos_in_byte=(7-pic->bitpos_in_compressed_pixeldata%8);
	uint16_t ret=0;
	uint_fast8_t bits_copied=0;
	
	while(pos_in_byte>=0 && bits_copied<nb_bits)
	{
		ret<<=1;
		ret|=!!(pic->compressed_pixeldata[index]&(1<<pos_in_byte));
		bits_copied++;
		pos_in_byte--;
		if(pos_in_byte<0)
		{
			pos_in_byte=7;
			index++;
		}
	}
	
	return ret;	
}

void bitstream_remove_bits(picture_t * const pic, const uint_fast8_t nb_bits)
{
	pic->bitpos_in_compressed_pixeldata+=nb_bits;
}


bool huff_decode(picture_t * const pic, const uint8_t Tc, const uint8_t Th, const uint8_t sz, const uint16_t bitstream, uint8_t * const decoded)
{
	uint32_t i;
	
	for(i=0; i<pic->huff_tables[Tc][Th].nb_entries; i++)
	{
		if(pic->huff_tables[Tc][Th].entries[i].sz==sz && pic->huff_tables[Tc][Th].entries[i].codeword==bitstream)
		{
			(*decoded)=pic->huff_tables[Tc][Th].entries[i].decoded;
			return true;
		}
	}
	
	return false;
}

bool bitstream_get_next_decoded_element(picture_t * const pic, const uint8_t Tc, const uint8_t Th, uint8_t * const decoded, uint_fast8_t * const nb_bits)
{
	uint16_t huff_candidate;
	bool found;
	
	while(pic->bitpos_in_compressed_pixeldata<8*pic->sz_compressed_pixeldata)
	{
		found=false;
		for(*nb_bits=1; *nb_bits<=16; (*nb_bits)++)
		{
			if((pic->bitpos_in_compressed_pixeldata+*nb_bits)>8*pic->sz_compressed_pixeldata)
				errx(1, "end of stream, requested to many bits");
			
			huff_candidate=bitstream_get_bits(pic, *nb_bits);
			
			if(huff_decode(pic, Tc, Th, *nb_bits, huff_candidate, decoded))
			{
				found=true;
				bitstream_remove_bits(pic, *nb_bits);
#ifdef PRINT_DETAILS_HUFFMAN_BITSTREAM
				printf("bitstream: decoded Huffman code %s (0x%x) (sz %u bits): %u (0x%x), new bitpos is %lu\n", to_bin(huff_candidate, *nb_bits), huff_candidate, *nb_bits, *decoded, *decoded, pic->bitpos_in_compressed_pixeldata);
#endif
				return true;
			}
		}
		if(!found)
		{
			//check if it's padding, else error
			bool is_all_one=true;
			uint_fast8_t i;
			for(i=0; i<(*nb_bits)-1; i++)
			{
				if((huff_candidate&(1<<i))==0)
				{
					is_all_one=false;
					break;
				}
			}
			
			if(is_all_one) //padding
				bitstream_remove_bits(pic, *nb_bits);
			else
				errx(1, "unknown code in bitstream bitpos %lu byte 0x%x [prev 0x%x, next 0x%x]", pic->bitpos_in_compressed_pixeldata, pic->compressed_pixeldata[pic->bitpos_in_compressed_pixeldata/8], pic->compressed_pixeldata[(pic->bitpos_in_compressed_pixeldata/8)-1], pic->compressed_pixeldata[(pic->bitpos_in_compressed_pixeldata/8)+1]);
		}
	}

	return false;
}

static void HAFFMANN_2MATREX (matrix8x8_t matrix, picture_t * const pic, int component, int16_t precedent_DC[4])
{
  uint_fast8_t ac_count;
  uint_fast8_t nb_bits;
  uint8_t SSSS;
  int16_t DC;
  uint_fast8_t u,v;

  if(!bitstream_get_next_decoded_element(pic, 0, pic->components_data[component].Td, &SSSS, &nb_bits))
    errx(1, "no DC data");
				
  if(SSSS)
  {
    uint16_t bits_DC=bitstream_get_bits(pic, SSSS);
    bitstream_remove_bits(pic, SSSS);
					
    bool msb_DC=!!(bits_DC&(1<<(SSSS-1)));
					
    if(msb_DC)
      DC=precedent_DC[component]+bits_DC;
    else
      DC=precedent_DC[component]+convert_to_neg(bits_DC,SSSS);
					
  }
  else
    DC=precedent_DC[component]+0;
				
  matrix[0][0]=DC;
  precedent_DC[component]=DC;
				
  int16_t AC;
  for(ac_count=0; ac_count<63; )
  {
    uint8_t RRRRSSSS;
    if(!bitstream_get_next_decoded_element(pic, 1, pic->components_data[component].Ta, &RRRRSSSS, &nb_bits))
      errx(1, "no AC data");
					
    uint8_t RRRR=(RRRRSSSS>>4); //number of preceding 0 samples
    uint8_t SSSS=RRRRSSSS&0x0f; //category
					
    if(RRRR==0 && SSSS==0)
    {
      break;
    }
    else if(RRRR==0x0F && SSSS==0)
    {
      ac_count+=16;
    }
    else
    {
      ac_count+=RRRR;
						
      uint16_t bits_AC=bitstream_get_bits(pic, SSSS);
      bitstream_remove_bits(pic, SSSS);
						
      bool msb_AC=!!(bits_AC&(1<<(SSSS-1)));
						
      if(msb_AC)
        AC=bits_AC;
      else
        AC=convert_to_neg(bits_AC,SSSS);
						
      u=(ac_count+1)/8;
      v=(ac_count+1)%8;
      matrix[u][v]=AC;
      ac_count++;
    }
  }
}
