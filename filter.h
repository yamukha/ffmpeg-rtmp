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

//float * do_kernel (int rs, float * koeff ,  int ones);
int  get_factor (float * koeff, int rs, float *sum);
int normalize_filter (float * kernel1d, int rs , float *sum);
int img_filter(int trick ,float * ikernel, int filterSize, int w, int h, int winw, int winh, int bits, uint8_t * pixbuff,  uint8_t * result );
int crop (unsigned char * image, int imagew, int imageh, unsigned char * crop_buffer, int cropw, int croph, int dw, int dh, int bits );
int overlay (unsigned char * image, int imagew, int imageh, unsigned char * crop_buffer, int cropw, int croph, int dw, int dh, int bits );
int fill_TGA_header (unsigned char * header, unsigned char type ,  int w, int  h ,int a) ;
float * do_kernel (int rs, float * koeff ,  int ones, float * factor);

#endif /* FILTER_H_ */
