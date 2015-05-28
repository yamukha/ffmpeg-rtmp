#ifndef UTILS_H
#define UTILS_H

#include "avheader.h"

#define min(X,Y) (((X) < (Y)) ? (X) : (Y))
#define max(X,Y) (((X) > (Y)) ? (X) : (Y))
long get_time_ms (void);
int kbhit(void);
void print_usage();
void pgm_save(uint8_t *buf, int wrap, int xsize, int ysize, int iFrame);
void SaveAFrames (AVFrame *pFrame, int channels );
void SaveVFrameRGB(AVFrame *pFrame, int width, int height, int iFrame);
int rescaleTimeBase(AVPacket *out_pkt, AVPacket *base_pkt, AVRational in_tb, AVRational out_tb );

#endif
