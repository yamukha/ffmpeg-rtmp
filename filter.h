/*
 * filter.h
 *
 *  Created on: May 26, 2015
 *      Author: amukha
 */

#ifndef FILTER_H_
#define FILTER_H_
#define TGA_HEADER_SIZE 18
#define TRICK_OFF  0
#define FILL_BY_1S 1
#define TRICK_COPY 2
#define LEVEL_WHITE 0
#define LEVEL_BLACK 255
#define WITH_BW_LEVELS 1
#define WITHOUT_BW_LEVELS 0
#define LOGO_POSX 5
#define LOGO_POSY 10

struct gfilter
{
   int  ox;
   int  oy;
   int  w;
   int  h;
   int  scale;
   int  bpp;
} ;
//float * do_kernel (int rs, float * koeff ,  int ones);
int  get_factor (float * koeff, int rs, float *sum);
int normalize_filter (float * kernel1d, int rs , float *sum);
int img_filter(int trick ,float * ikernel, int filterSize, int w, int h, int winw, int winh, int bits, volatile uint8_t * pixbuff,  volatile uint8_t * result );
int crop (volatile uint8_t * image, int imagew, int imageh, volatile uint8_t * crop_buffer, int cropw, int croph, int dw, int dh, int bits );
int overlay (volatile uint8_t * image, int imagew, int imageh, volatile uint8_t * crop_buffer, int cropw, int croph, int dw, int dh, int bits , int alpha );
int fill_TGA_header (unsigned char * header, unsigned char type ,  int w, int  h ,int a) ;
float * do_kernel (int rs, float * koeff ,  int ones, float * factor);
int  smooth ( uint8_t*  iimg, uint8_t*  buf, int inw, int inh, int scale , int bytesPerPixel);

#endif /* FILTER_H_ */
