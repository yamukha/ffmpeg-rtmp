#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>

long get_time_ms (void)
{
    long            ms; // Milliseconds
    time_t          s;  // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s  = spec.tv_sec;
    ms = s * 1000 + round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds

    return ms;
}

int kbhit(void)
{
  struct termios oldt, newt;
  int ch;
  int oldf;

  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  ch = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);

  if(ch != EOF &&  ' '== ch )
  {
     return 1;
  }

  return 0;
}

void print_usage()
{
  fprintf (stdout,
           "demuxes media input to rpmt streams\n"
           "usage:  ./ffstream input destination format \n"
           "i.e."
           "./ffstream rtmp://ev1.favbet.com/live/stream26 rtmp://127.0.0.1/live/mystream flv \n"
         );
}

void pgm_save(uint8_t *buf, int wrap, int xsize, int ysize, int iFrame)
{
    FILE *f;
    int i;
    char filename[32];

    sprintf(filename, "frame%d.ppm", iFrame);
    f=fopen(filename,"w");
    fprintf(f,"P5\n%d %d\n%d\n",xsize,ysize,255);
    for(i=0;i<ysize;i++)
       fwrite(buf + i * wrap,1,xsize,f);
     fclose(f);
     printf ("frame written \n");
 }

int rescaleTimeBase(AVPacket *out_pkt, AVPacket *base_pkt, AVRational in_tb, AVRational out_tb )
{
    if (out_pkt->pts != AV_NOPTS_VALUE)
        out_pkt->pts = av_rescale_q(base_pkt->pts, in_tb,out_tb );
    if (out_pkt->dts != AV_NOPTS_VALUE)
        out_pkt->dts = av_rescale_q(base_pkt->dts, in_tb, out_tb);
   //  fprintf (stdout, "pts %ld, dts %ld  \n",out_pkt.pts, out_pkt.dts);
    return 0;
}

void SaveAFrames (AVFrame *pFrame, int channels )
{
    FILE *pFile;
    char filename[32];
    int  y;
    // Open file
    sprintf(filename, "audio.raw");
    pFile=fopen(filename, "a");
    if(pFile==NULL)
        return;
    int i, ch;
    for (i=0; i< pFrame->nb_samples ; i++)
        for (ch=0; ch < channels; ch++)
    fwrite(pFrame->data[ch] + channels*i, 1, channels, pFile);
    fclose(pFile);
}

void SaveVFrameRGB(AVFrame *pFrame, int width, int height, int iFrame)
{
    FILE *pFile;
    char filename[32];
    int  y;
    // Open file
    sprintf(filename, "frameRGB%d.ppm", iFrame);
    pFile=fopen(filename, "wb");
    if(pFile==NULL)
        return;
    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for(y=0; y<height; y++)
        fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile); // width*3
    fclose(pFile);
    printf ("RGB frame written \n");
}
