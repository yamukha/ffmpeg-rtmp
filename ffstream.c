#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <pthread.h>
#include <unistd.h>

#include "utils.h"

#define MAX 1
#define LIVE_STREAM 
//#define COPY_PACKETS

static void print_usage() 
{
  fprintf (stdout,
           "demuxes media input to rpmt streams\n"
           "usage:  ./ffstream input destination format \n"
           "i.e."
           "./ffstream rtmp://ev1.favbet.com/live/stream26 rtmp://127.0.0.1/live/mystream flv \n"
         );
}

AVOutputFormat *ofmt[MAX], *ofmte[MAX];
AVFormatContext *ifmt_ctx[MAX], *ofmt_ctx[MAX], *ofmte_ctx[MAX];
AVFormatContext *oc;
int video_idx[MAX], audio_idx[MAX];
AVPacket pkt[MAX];
static int static_pts = 0;

int nextPTS()
{
    return static_pts ++;
}
void SaveVFrame(AVFrame *pFrame, int width, int height, int iFrame)
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

static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize, int iFrame)
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

void* worker_thread(void *Param)
{
    int id = *((int*)Param);
    int idx = video_idx[id];
    int idxa = audio_idx[id];
    int ret;
    int frameCount = 0;
    int pktCount = 0;

    AVCodecParserContext* parser;
    AVFrame *pinFrame;
    AVFrame *pFrameRGB;
    AVFrame *poutFrame;
    struct SwsContext *img_convert_ctxi = NULL;
    struct SwsContext *img_convert_ctxo = NULL;

    while (1)
    {
        ret = av_read_frame(ifmt_ctx[id], &pkt[id]);
        if (ret < 0)
            break;

        int aindx = pkt[id].stream_index;

        if (pkt[id].stream_index == ( idxa ))
        {
            AVStream *in_stream = ifmt_ctx[id]->streams[pkt[id].stream_index];
            AVStream *out_stream = ofmt_ctx[id]->streams[pkt[id].stream_index];
            AVRational time_base = ifmt_ctx[id]->streams[idxa]->time_base;

#ifdef LIVE_STREAM         
           int time = 1000;
#else    
           int time = 1000 * 1000 * strtof(av_ts2timestr(pkt[id].duration, &time_base), NULL);           
#endif            
            usleep(time);
        
            pkt[id].pts = av_rescale_q_rnd(pkt[id].pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
            pkt[id].dts = av_rescale_q_rnd(pkt[id].dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
            pkt[id].duration = av_rescale_q(pkt[id].duration, in_stream->time_base, out_stream->time_base);
            pkt[id].pos = -1;
            pkt[id].stream_index = idxa;
#ifdef COPY_PACKETS
           ret = av_interleaved_write_frame(ofmt_ctx[id], &pkt[id]);
#else
           ret = av_interleaved_write_frame(oc, &pkt[id]);
#endif
            if (ret < 0)
            {
                fprintf(stderr, "Error muxing packet thread %d\n", id);
                break;
            }     
        
            av_free_packet(&pkt[id]);
            continue;
		}

        // video packets handler
        if (pkt[id].stream_index != idx)
            continue;

        AVStream *in_stream = ifmt_ctx[id]->streams[pkt[id].stream_index];
        AVStream *out_stream = ofmt_ctx[id]->streams[pkt[id].stream_index];
        AVRational time_base = ifmt_ctx[id]->streams[idx]->time_base;
        AVStream *enc_stream = oc->streams[pkt[id].stream_index];

        int ih = in_stream->codec->height;
        int iw = in_stream->codec->width;
        int ph = in_stream->codec->height;
        int pw = in_stream->codec->width;
        int oh = in_stream->codec->height;
        int ow = in_stream->codec->width;
        int got_packet = 0;
        int frameFinished;

        //AV_PIX_FMT_YUV420P AV_PIX_FMT_RGB24 AV_PIX_FMT_BGRA AV_PIX_FMT_YUVJ420P

        enum AVPixelFormat oPixFormat = AV_PIX_FMT_YUV420P;
        enum AVPixelFormat pPixFormat = AV_PIX_FMT_RGB24;
        enum AVPixelFormat iPixFormat = in_stream->codec->pix_fmt;

        pinFrame=av_frame_alloc(); // Allocate video frame
        pFrameRGB=av_frame_alloc();
        poutFrame=av_frame_alloc();

        parser = av_parser_init(AV_CODEC_ID_H264);
        if(!parser) {
            fprintf(stderr,"Erorr: cannot create H264 parser.\n");
            exit (-1);
        }

        int num_inBytes=avpicture_get_size(iPixFormat, iw, ih);
        uint8_t* in_buffer=(uint8_t *)av_malloc(num_inBytes*sizeof(uint8_t));
        avpicture_fill((AVPicture *)pinFrame, in_buffer,iPixFormat, iw, ih);

        int num_BytesRGB=avpicture_get_size(pPixFormat, ow, oh);
        uint8_t* bufferRGB=(uint8_t *)av_malloc(num_BytesRGB*sizeof(uint8_t));
        avpicture_fill((AVPicture *)pFrameRGB, bufferRGB,pPixFormat, pw, ph);

        int num_outBytes=avpicture_get_size(oPixFormat, ow, oh);
        uint8_t* out_buffer=(uint8_t *)av_malloc(num_outBytes*sizeof(uint8_t));
        avpicture_fill((AVPicture *)poutFrame, out_buffer,oPixFormat, ow, oh);

        avcodec_decode_video2(in_stream->codec, pinFrame, &frameFinished, &pkt[id]);
        if(frameFinished) {

         if(img_convert_ctxi == NULL)
             img_convert_ctxi = sws_getContext(iw, ih, iPixFormat, pw, ph,pPixFormat,SWS_BICUBIC, NULL, NULL, NULL);

         sws_scale(img_convert_ctxi, pinFrame->data, pinFrame->linesize, 0, ih , pFrameRGB->data, pFrameRGB->linesize);

         int jj ;
         // change bits in pix map
         for (jj = 0 ; jj < num_BytesRGB/8 ; )
         {
            bufferRGB [jj] = 255;
            bufferRGB [jj+1] = 255;
            bufferRGB [jj+2] = 255;
            bufferRGB [jj+3] = 255;
            jj +=4;
         }

         poutFrame->height = 480;
         poutFrame->width = 640;
         poutFrame->format = (int) oPixFormat;

         if(img_convert_ctxo == NULL)
             img_convert_ctxo = sws_getContext(pw, ph, pPixFormat, ow, oh,oPixFormat,SWS_BICUBIC, NULL, NULL, NULL);
         sws_scale(img_convert_ctxo, pFrameRGB->data, pFrameRGB->linesize, 0, ph , poutFrame->data, poutFrame->linesize);

         //if ( 1 == frameCount )
         if ( kbhit ())
         {
             pgm_save(pinFrame->data[0], pinFrame->linesize[0],iw,ih, frameCount);
             pgm_save(poutFrame->data[0], poutFrame->linesize[0],ow,oh, frameCount+1);
             SaveVFrame (pFrameRGB, pw, ph, frameCount);
         }
         frameCount++;
        }

#ifdef LIVE_STREAM    
        int time = 1000;         
#else
        int time = 1000 * 1000 * strtof(av_ts2timestr(pkt[id].duration, &time_base), NULL);
#endif        
        usleep(time);
        //fprintf(stdout, "PTS in %ld \n",pkt[id].pts);
        //pkt[id].pts = av_rescale_q_rnd(pkt[id].pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        //pkt[id].dts = av_rescale_q_rnd(pkt[id].dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        //pkt[id].duration = av_rescale_q(pkt[id].duration, in_stream->time_base, out_stream->time_base);
        //pkt[id].pos = -1;
        //pkt[id].stream_index = idx;

        if(frameFinished)
        {

            AVPacket opkt ;
            AVPacket vpkt ;
            opkt.data = NULL;
            opkt.size = 0;
            opkt.flags |= AV_PKT_FLAG_KEY;
            opkt.pts = opkt.dts = pktCount;
            av_init_packet(&opkt);
            opkt.pos = -1;
            oc->streams [pkt[id].stream_index]->codec->coded_frame->pts = pktCount;

           // opkt.stream_index  = idx;
           //opkt.data          = (uint8_t *)out_buffer;
           //opkt.size          = sizeof(AVPicture);
           //opkt.pts = pkt.dts = pFrame->pts;
           //opkt.pts =   pkt[id].pts ;
           //opkt.dts = pkt[id].dts;
           //opkt.duration = pkt[id].duration;


          // enc_stream->time_base.den = 50;
          // opkt.stream_index = idx;

          //opkt.pts = av_rescale_q_rnd(pkt[id].pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
          //opkt.dts = av_rescale_q_rnd(pkt[id].dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
          //opkt.duration = av_rescale_q(pkt[id].duration, in_stream->time_base, out_stream->time_base);

           poutFrame->pts = pktCount;
           ret = avcodec_encode_video2( oc->streams [pkt[id].stream_index]->codec, &opkt, poutFrame, &got_packet);

           //av_packet_rescale_ts(&opkt, in_stream->codec->time_base, oc->streams [pkt[id].stream_index]->codec->time_base);

           if (ret < 0)  {
                fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
           }

           if (got_packet)
           {

           if(oc->streams [pkt[id].stream_index]->codec->coded_frame->key_frame)
           {
                opkt.flags |= AV_PKT_FLAG_KEY;
           }
               if (opkt.pts != AV_NOPTS_VALUE)
                   opkt.pts = av_rescale_q(pkt[id].pts, in_stream->time_base, out_stream->time_base);
               if (opkt.dts != AV_NOPTS_VALUE)
                  opkt.dts = av_rescale_q(pkt[id].dts,  in_stream->time_base, out_stream->time_base);
           fprintf (stdout, "pts %ld, dts %ld  \n",opkt.pts, opkt.dts);

#ifndef COPY_PACKETS
               ret = av_interleaved_write_frame(oc, &opkt);
               if (ret < 0)
               {
                 fprintf(stderr, "Error writing video frame: %s\n", av_err2str(ret));
               }
#endif
               av_free_packet(&opkt);
               fprintf(stderr, "write frame result: %s\n", av_err2str(ret));
           }

#ifdef COPY_PACKETS
           ret = av_interleaved_write_frame(ofmt_ctx[id], &pkt[id]);
#else
#endif

           av_free_packet(&pkt[id]);
           av_frame_free(&pinFrame);
           av_frame_free(&pFrameRGB);
           av_frame_free(&poutFrame);
           av_free (bufferRGB);
           av_free (in_buffer);
           av_free (out_buffer);

       } // frameFinished
        pktCount++;

       // if (got_packet){
       //     ret = av_interleaved_write_frame(ofmt_ctx[id], &pkt[id]);
       // }

       ret = 0;
       if (ret < 0)
       {
           fprintf(stderr, "Error muxing packet thread %d\n", id);
           break;
       }

       //av_free_packet(&pkt[id]);
    }
}

int main(int argc, char **argv)
{
    char *in_filename, *destination, *out_format ;
    char out_filename[64];
    int ret, i = 0;

    AVCodecContext  *pVCodecCtx, *pACodecCtx;
    AVCodecContext  *pVencCtx, *pAencCtx;
    AVCodec         *pVCodec,  *pACodec;
    AVCodec         *pAenc, *pVenc;
    AVStream *audio_st, *video_st;

    static int video_stream_idx = -1, audio_stream_idx = -1;

    memset(ofmt, 0, sizeof(ofmt));
    memset(ofmt_ctx, 0, sizeof(ofmt_ctx));
    memset(ofmte, 0, sizeof(ofmte));
    memset(ofmte_ctx, 0, sizeof(ofmte_ctx));
    memset(ifmt_ctx, 0, sizeof(ifmt_ctx));
    memset(video_idx, -1, sizeof(video_idx));

    if  (argc != 4)
    {
        print_usage();
        exit (0);    
    }
    
    in_filename  = argv[1];
    destination = argv[2];
    out_format = argv[3];

    av_register_all();
    avformat_network_init();

    for (i = 0; i < MAX; i++)
    {
        fprintf(stdout, "#%d\n", i);

        if ((ret = avformat_open_input(&ifmt_ctx[i], in_filename,  NULL,  NULL)) < 0)
        {
            fprintf(stderr, "Could not open input file '%s'", in_filename);
            goto end;
        }

        if ((ret = avformat_find_stream_info(ifmt_ctx[i], 0)) < 0)
        {
            fprintf(stderr, "Failed to retrieve input stream information");
            goto end;
        }

        ifmt_ctx[i]->flags |= AVFMT_FLAG_GENPTS;

        sprintf(out_filename,"%s", destination);
        printf("output: video=%s  \n", out_filename );

        avformat_alloc_output_context2(&ofmt_ctx[i], NULL, out_format , out_filename);
        if (!ofmt_ctx[i])
        {
            fprintf(stderr, "Could not create output context\n");
            goto end;
        }

        avformat_alloc_output_context2(&ofmte_ctx[i], NULL, out_format , out_filename);
        if (!ofmte_ctx[i])
        {
             fprintf(stderr, "Could not create output encoder context\n");
             goto end;
        }

        ofmt[i] = ofmt_ctx[i]->oformat;
        ofmte[i] = ofmte_ctx[i]->oformat;
           
        int k;
        int res;
        for (k = 0; k < ifmt_ctx[i]->nb_streams; k++)
        {
            if (ifmt_ctx[i]->streams[k]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                // Add the audio and video streams using the default format codecs and initialize the codecs.
                // video_st = NULL;
                // audio_st = NULL;
                if (ofmte[i]->video_codec != AV_CODEC_ID_NONE) {
                    //video_st = add_stream(&ofmte_ctx[i], &pVenc, ofmte[i]->video_codec);
                    pVenc = avcodec_find_encoder(AV_CODEC_ID_H264);
                    if(NULL == pVenc)
                        { fprintf(stderr,"Could not find needed video encoder\n"); exit(1); }

                    //oc = avformat_alloc_context();
                    avformat_alloc_output_context2(&oc, NULL, out_format, out_filename);
                    oc->video_codec_id = AV_CODEC_ID_H264;
                    oc->audio_codec_id = AV_CODEC_ID_NONE;
                      //context->bit_rate = m_videoOptions.bitrate * 1024;
                    oc->bit_rate = 90000;
                    oc->flags |= AVFMT_GLOBALHEADER;

                    pVencCtx= avcodec_alloc_context3(pVenc);

                    video_st = avformat_new_stream(oc, pVenc);
                    if (!video_st) {
                        fprintf(stderr, "Could not allocate video stream\n");
                       exit(1);
                    }
                    video_st->id = oc->nb_streams-1;
                    video_st->sample_aspect_ratio = av_d2q(1, 255);
                    video_st->pts_wrap_bits = 33;
                    pVencCtx = video_st->codec;
                    pVencCtx->codec_id = AV_CODEC_ID_H264;
                    pVencCtx->codec_type = AVMEDIA_TYPE_VIDEO;
                    pVencCtx->pix_fmt = AV_PIX_FMT_YUV420P;
                    pVencCtx->width = 640;
                    pVencCtx->height = 480;//
                    pVencCtx->bit_rate = pVencCtx->width *pVencCtx->height * 4;
                    //pVencCtx->bit_rate = 90000;
                    //pVencCtx->time_base= tb;
                    pVencCtx->time_base.den = 25;
                    pVencCtx->time_base.num = 1;
                    pVencCtx->gop_size = pVencCtx->time_base.den;
                    pVencCtx->pix_fmt = AV_PIX_FMT_YUV420P;
                    pVencCtx->flags |= CODEC_FLAG_GLOBAL_HEADER | CODEC_FLAG_LOOP_FILTER | CODEC_FLAG_PASS1;
                    pVencCtx->keyint_min = pVencCtx->gop_size;

                    pVencCtx->scenechange_threshold = 40;
                    pVencCtx->sample_aspect_ratio = av_d2q(1, pVencCtx->width / pVencCtx->height);

                    pVencCtx->refs = 8;
                    pVencCtx->qblur = 0.5;
                    pVencCtx->qcompress = 0.6;
                    pVencCtx->b_quant_factor = 1.10;
                    pVencCtx->i_quant_factor = -1.10;
                    pVencCtx->qmax = 39;//51;
                    pVencCtx->qmin = 2;//10;
                    pVencCtx->max_qdiff = 2;
                    pVencCtx->me_range = 64;

                    pVencCtx->coder_type = FF_CODER_TYPE_VLC;
                    pVencCtx->thread_count = 1;//m_globalData.GetConfig().Video().VideoThreads();
                    pVencCtx->max_b_frames = 3; //???
                    pVencCtx->me_method = ME_FULL;

                    AVDictionary* dict = NULL;
                    av_dict_set(&dict, "tune", "zerolatency", 0);
                    av_dict_set(&dict, "subq", "9", 0);
                    av_dict_set(&dict, "coder", "1", 0);
                    av_dict_set(&dict, "mbtree", "1", 0);
                    av_dict_set(&dict, "dct8x8", "1", 0);
                    av_dict_set(&dict, "partitions", "i8x8", 0);

                    pVencCtx->flags |= CODEC_FLAG_GLOBAL_HEADER | CODEC_FLAG_LOOP_FILTER | CODEC_FLAG_PASS1;

                    if (avcodec_open2(pVencCtx, pVenc, NULL) < 0) {
                       fprintf(stderr, "Could not open video codec\n");
                       exit(1);
                   }
                }
                // the same for audio transcoding

                //Get a pointer to the codec context for the video stream
                pVCodecCtx=ifmt_ctx[i]->streams[k]->codec;
                pVCodec=avcodec_find_decoder(pVCodecCtx->codec_id);

                if(pVCodec==NULL)
                    { fprintf(stderr,"Could not find needed video codec\n"); return -1; }

                if( avcodec_open2(pVCodecCtx, pVCodec, NULL) < 0)
                     {  fprintf(stderr,"Can not open video codec \n");  return -1; }
                else
                    {  fprintf(stdout,"Be used video codec #%d \n", (int)pVCodecCtx->codec_id ); }

                AVStream *in_stream = ifmt_ctx[i]->streams[k];
                AVStream *out_stream = avformat_new_stream(ofmt_ctx[i], in_stream->codec->codec);


                if (!out_stream)
                {
                    fprintf(stderr, "Failed allocating output stream\n");
                    ret = AVERROR_UNKNOWN;
                    goto end;
                }

                ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
                if (ret < 0)
                {
                    fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
                    goto end;
                }

                AVRational timeBase = out_stream->codec->time_base;

                out_stream->time_base = timeBase;
                out_stream->codec->codec_tag = 0;
                if (ofmt_ctx[i]->oformat->flags & AVFMT_GLOBALHEADER)
                    { out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;}

                video_st->time_base = timeBase;
                video_st->codec->codec_tag = 0;
                if (oc->oformat->flags & AVFMT_GLOBALHEADER)
                    { video_st->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;}

                video_idx[i] = k;
                fprintf(stdout, "input video @stream #%d restreamed to\n",video_idx[i] );
                av_dump_format(ofmt_ctx[i], 0,  out_filename, 1);
            }

            if (ifmt_ctx[i]->streams[k]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
            {

                 pACodecCtx=ifmt_ctx[i]->streams[k]->codec;
                 pACodec=avcodec_find_decoder(pACodecCtx->codec_id);

                 if(pACodec==NULL)
                    { fprintf(stderr,"Could not find needed audio codec\n"); return -1; }

                 if( avcodec_open2(pACodecCtx, pACodec, NULL) < 0)
                    {  fprintf(stderr,"Can not open audio codec \n");  return -1; }
                 else
                    {  fprintf(stdout,"Be used audio codec #%d \n", (int)pACodecCtx->codec_id ); }

                AVStream *ina_stream = ifmt_ctx[i]->streams[k];
                AVStream *out_stream = avformat_new_stream(ofmt_ctx[i], ina_stream->codec->codec);

                audio_st = avformat_new_stream(oc, ina_stream->codec->codec);
                ret = avcodec_copy_context(audio_st->codec, ina_stream->codec);
                if (ret < 0)
                {
                     fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
                     goto end;
                }

                if (!out_stream)
                {
                   fprintf(stderr, "Failed allocating output stream\n");
                   ret = AVERROR_UNKNOWN;
                   goto end;
                }

                ret = avcodec_copy_context(out_stream->codec, ina_stream->codec);
                if (ret < 0)
                {
                    fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
                    goto end;
                }

                AVRational timeBase = out_stream->codec->time_base;
                out_stream->time_base = timeBase;

                audio_st->time_base = timeBase;
                audio_st->codec->codec_tag = 0;
                if (oc->oformat->flags & AVFMT_GLOBALHEADER)
                    { audio_st->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;}

                out_stream->codec->codec_tag = 0;
                if (ofmt_ctx[i]->oformat->flags & AVFMT_GLOBALHEADER)
                    { out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;}

                audio_idx[i] = k;
                fprintf(stdout, "input audio @stream #%d restreamed to\n",audio_idx[i+1]);
            }
        } // for

         if (!(ofmt[i]->flags & AVFMT_NOFILE))
        {
#ifdef COPY_PACKETS
            ret = avio_open(&ofmt_ctx[i]->pb, out_filename, AVIO_FLAG_WRITE);
#else
            ret = avio_open(&oc->pb, out_filename, AVIO_FLAG_WRITE);
#endif
            if (ret < 0)
            {
                fprintf(stderr, "Could not open output file '%s'", out_filename);
                goto end;
            }
        }

        fprintf(stdout, "Dump output format\n");
#ifdef  COPY_PACKETS
        ret = avformat_write_header(ofmt_ctx[i], NULL);
        av_dump_format(ofmt_ctx[i], 0, out_filename, 1);
#else
        ret = avformat_write_header(oc, NULL);
        av_dump_format(oc, 0,  out_filename, 1);
#endif
        if (ret < 0)
        {
            fprintf(stderr, "Error occurred when opening output file\n");
            goto end;
        }
    }

    fprintf(stdout, "start threads\n");
    pthread_t thread_ids[MAX];
    for (i = 0; i < MAX; i++)
    {
        if ( 0 != pthread_create(&thread_ids[i], NULL, &worker_thread, (void*)&i) )
        {
            fprintf(stderr, "Could not create thread %d\n", i);
            break;
        }
    }

    for (i = 0; i < MAX; i++)
    {
        pthread_join(thread_ids[i], NULL);
    }

end:
    for (i = 0; i < MAX; i++)
    {
#ifdef COPY_PACKETS
        av_write_trailer(ofmt_ctx[i]);
#else
        av_write_trailer(oc);
#endif
        avformat_close_input(&ifmt_ctx[i]);
#ifdef COPY_PACKETS
        if (ofmt_ctx[i] && !(ofmt[i]->flags & AVFMT_NOFILE))
            avio_closep(&ofmt_ctx[i]->pb);
        avformat_free_context(ofmt_ctx[i]);
#else
        if (oc && !(ofmte[i]->flags & AVFMT_NOFILE))
           avio_closep(&oc->pb);
        avformat_free_context(oc);
#endif

        if (ret < 0 && ret != AVERROR_EOF)
        {
            fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
            return 1;
        }
    }
    return 0;
}
