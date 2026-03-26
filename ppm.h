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

uint8_t clamp(const double v)
{
	if(v<0)
		return 0;
	if(v>255)
		return 255;
	
	return (uint8_t)v;
}

void write_ppm(picture_t const * const pic, char const * const filename)
{
	uint_fast16_t x,y;
	
	double Y,Cb,Cr;
	double r,g,b;
	
	FILE *out=fopen(filename, "w");
	
	printf("writing file %s\n", filename);
	
	fprintf(out, "P3\n%lu %lu\n255\n", pic->size_X, pic->size_Y);
		
	for(y=0; y<pic->size_Y; y++)
	{
		for(x=0; x<pic->size_X; x++)
		{
			Y=pic->pixels_YCbCr[x][y].Y;
			Cb=pic->pixels_YCbCr[x][y].Cb;
			Cr=pic->pixels_YCbCr[x][y].Cr;
			
			r=Y+1.402*(Cr-128);
			g=Y-(0.114*1.772*(Cb-128)+0.299*1.402*(Cr-128))/0.587;
			b=Y+1.772*(Cb-128);
			
			fprintf(out, "%u %u %u ", clamp(round(r)),clamp(round(g)),clamp(round(b)));
		}
		fprintf(out, "\n");
	}
	fclose(out);
	printf("output file written\n\n");
}
