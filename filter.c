#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "utils.h"
#include "filter.h"

//#include "stb_defs.h"

int get_factor (float * kernel1d, int rs, float *sum)
{
     if ( 1 > rs)
        return -1;

    int i;
    for (i = 0; i < rs * rs; i++)
    {
        printf ( " %f ", kernel1d[i ] );
        *sum += kernel1d [i ];
        if ( (rs - 1) == i % rs)
            printf ("\n");
    }
    return 0;
}

int normalize_filter (float * kernel1d, int rs , float * sum)
{
     if ( 1 > rs)
        return -1;

    int i;
    for (i = 0; i < rs * rs; i++)
    {
        kernel1d[i]/= sum [0];
        printf ( " %f ", kernel1d[i ] );
        if ( (rs - 1) == i % rs)
            printf ("\n");
    }
    return 0;
}

int img_filter(int trick ,float * ikernel, int filterSize, int w, int h, int winw, int winh, int bits, volatile uint8_t * pixbuff, volatile  uint8_t * result )
{
    int filterSizeH = filterSize;
    int filterSizeW = filterSize;
    int rs = filterSize;

    float nfactor = 1.0;
    if (!trick )
        nfactor = 1.0;
    else
        nfactor = 1.0/9.0;

    float bias = 0.0;

    float fkernel [filterSize] [filterSize] ;
    int i, j;
    if (!trick)
    {
        for (i = 0; i < rs; i++)
        {
            for (j = 0; j < rs; j++)
            {
                fkernel [i] [j] = ikernel[i * rs + j];
            }
        }
    }

    int x, y;
    if ( TRICK_COPY == trick)
    {
        for( x = 0; x < w; x++)
        {
             for(y = 0; y < h; y++)
                 pixbuff [y*w*bits + x*bits  ] = result [y*w*bits + x*bits  ];
         }
         return 0;
    }

    for( x = 0; x < w; x++)
    {
        for(y = 0; y < h; y++)
        {
            float red = 0.0, green = 0.0, blue = 0.0, alpha = 0.0;

            //multiply every value of the kernel with corresponding image pixel
            int filterX;
            for(filterX = 0; filterX < filterSizeW; filterX++)
            {
                int filterY;
                for(filterY = 0; filterY < filterSizeH; filterY++)
                {
                    int imageX = (x - filterSizeW / 2 + filterX + w) % w;
                    int imageY = (y - filterSizeH / 2 + filterY + h) % h;
                    if (trick)
                    {
                         red += ( pixbuff [imageY * w * bits + imageX *bits] );
                         green += ( pixbuff [imageY * w * bits + imageX *bits + 1] ) ;
                         blue += ( pixbuff [imageY * w * bits + imageX * bits + 2] ) ;
                        // alpha += ( pixbuff [imageY * w * bits + imageX *bits + 3] ) ;
                    }
                    else
                    {
                        red += ( pixbuff [imageY * w * bits + imageX *bits] ) *  fkernel[filterX][filterY];
                        green += ( pixbuff [imageY * w * bits + imageX *bits + 1] ) * fkernel[filterX][filterY];
                        blue += ( pixbuff [imageY * w * bits + imageX * bits + 2] ) * fkernel[filterX][filterY];
                        //alpha += ( pixbuff [imageY * w * bits + imageX *bits + 3] ) * fkernel[filterX][filterY];
                    }
                }
            }

            result [y*w*bits + x*bits  ]   = min(max ((int) (nfactor * red + bias), 0), 255);
            result [y*w*bits + x*bits +1 ] = min(max ((int) (nfactor * green + bias),0), 255);
            result [y*w*bits + x*bits +2 ] = min(max ((int) (nfactor * blue + bias), 0), 255);
            result [y*w*bits + x*bits +3 ] = min(max ((int) (nfactor * alpha + bias),0), 255);
        }
    }

    for( x = 0; x < w; x++)
    {
        for(y = 0; y < h; y++)
            pixbuff [y*w*bits + x*bits  ] = result [y*w*bits + x*bits  ];
    }
    // memcpy (pixbuff, result, w * bits * h);
    return 0;
}


int crop ( volatile uint8_t * image, int imagew, int imageh, volatile uint8_t * crop_buffer, int cropw, int croph, int dw, int dh, int bits )
{
    int i, j;
    int start_pos, end_pos, delta_pos;

    if (cropw + dw> imagew){
        printf ("cropw +dw > imgw\n");
        return -1;
    }

    if (croph + dh> imageh){
        printf ("croph +dh > imageh\n");
        return -1;
    }

    start_pos = croph * bits * imagew  + cropw * bits;
    delta_pos = dh * bits * imagew  + dw * bits;
    end_pos = start_pos +  delta_pos;

    j = 0;
    int k = 0;
    for( i = start_pos; i < end_pos; i++)
    {
        int line;
        if (i % (imagew * bits) == cropw * bits)
        {
           if (line >= dh)
               break;
           int delta;
           for ( delta = 0; delta < dw * bits; delta++ )
           {
               //if ( 255 != crop_buffer [line * bits * dw + delta ] && 0 != crop_buffer [line * bits * dw + delta ])
                   crop_buffer [j] =  image [i] ;
               i++; j++;
           }
           line++;
        }
    }
   // printf (" start_pos = %d, end_pos %d , copied %d bytes \n", start_pos, end_pos, j);
    return 0;
}

int overlay (volatile uint8_t* image, int imagew, int imageh, volatile uint8_t * crop_buffer, int cropw, int croph, int dw, int dh, int bits, int bw )
{
    int i, j;
    int start_pos, end_pos, delta_pos;

    if (cropw + dw> imagew){
        printf ("cropw +dw > imgw\n");
        return -1;
    }

    if (croph + dh> imageh){
        printf ("croph +dh > imageh\n");
        return -1;
    }

    start_pos = croph * bits * imagew  + cropw * bits;
    delta_pos = dh * bits * imagew  + dw * bits;
    end_pos = start_pos +  delta_pos;

    j = 0;
    int k = 0;
    for( i = start_pos; i < end_pos; i++)
    {
        int line;
        if (i % (imagew * bits) == cropw * bits)
        {
           if (line >= dh)
               break;
           int delta;
           for ( delta = 0; delta < dw * bits; delta++ )
           {
                if (bw)
                {
                    if ( LEVEL_WHITE != crop_buffer [line * bits * dw + delta ] && LEVEL_BLACK != crop_buffer [line * bits * dw + delta ])
                    image [i] = crop_buffer [line * bits * dw + delta ];
                }
                else
                    image [i] = crop_buffer [line * bits * dw + delta ];
                i++;
           }
           line++;
        }
    }
    //printf (" start_pos = %d, end_pos %d , copied %d bytes \n", start_pos, end_pos, j);
    return 0;
}

int fill_TGA_header (unsigned char * header, unsigned char type ,  int w, int  h ,int a) {
    header [TGA_HEADER_SIZE + 2] = type;

    header [TGA_HEADER_SIZE - 6 ] = w /256;
    header [TGA_HEADER_SIZE - 5 ] = w %256;
    header [TGA_HEADER_SIZE - 4 ] = h /256;
    header [TGA_HEADER_SIZE - 3 ] = h %256;
    if (a)
           header [TGA_HEADER_SIZE - 2] = 32;
        else
           header [TGA_HEADER_SIZE - 2] = 24;

    int i ;
    printf ("TGA header: ");
    for (i = 0; i < TGA_HEADER_SIZE; i++)
        printf ("%d ",header[i]);
    printf ("\n");
}

float * do_kernel (int rs, float * koeff ,  int ones, float * factor)
{
    float s2 = rs * rs * 2;
    float sum = 0;

    int kernelSize = rs * rs;
    printf ( "kernel size %d, %d\n", kernelSize, rs);
    int kernelSizeHalf = kernelSize / 2;

    float * kernel1d = (float * )malloc ( rs * rs * sizeof(float ));

    kernel1d[kernelSizeHalf]=1;
    kernel1d[0]=0;
    kernel1d[rs *rs -1]=0;
    int i, j;
    for (i = 1; i < kernelSizeHalf; i++)
    {
        float res = expf((float) - i * (float)i / s2);
        kernel1d[kernelSizeHalf + i] = res;
        kernel1d[kernelSizeHalf - i] = res;
    }

    for (i = 0; i < rs * rs; i++)
        sum += kernel1d [i ];

    factor [0]= 1.0;
    printf ("sum %f factor = %f\n", sum, factor[0]);

    printf ("original \n");
    for (i = 0; i < kernelSize; i++)
    {
        printf ( " %f ", kernel1d [i]);
        koeff [i ] = kernel1d [i] / sum ;
        if ( (rs - 1) == i % rs)
            printf ("\n");
    }
    free (kernel1d);
    return koeff;
}

