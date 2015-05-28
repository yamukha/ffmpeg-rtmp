#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "libavutil/opt.h"
#include "libavutil/fifo.h"

#include <gd.h>

#define MATRIX_SIZE 3
#define KERNEL_HALF_SIZE ((MATRIX_SIZE * MATRIX_SIZE + 1)/2)
#define FILL1S 1
#define LEVEL_WHITE 0
#define LEVEL_BLACK 255

#include <pthread.h>
#include <unistd.h>

#include "utils.h"
#include "filter.h"
#include "stb_defs.h"

#define MAX 1
#define LIVE_STREAM 
//#define COPY_VPACKETS
#define COPY_APACKETS

char *in_filename, *destination, *out_format ;
char out_filename[64];
AVOutputFormat *ofmt[MAX], *ofmte[MAX];
AVFormatContext *ifmt_ctx[MAX], *ofmt_ctx[MAX], *ofmte_ctx[MAX];
AVFormatContext *ovc;
AVFormatContext *oac;
AVCodecContext  *pVCodecCtx, *pACodecCtx;
AVCodecContext  *pVencCtx, *pAencCtx;
AVCodec         *pVCodec,  *pACodec;
AVCodec         *pAenc, *pVenc;
AVStream *audio_st, *video_st;
unsigned char* pix_buffer;
int imgh;
int imgw;
int bytesPerPixel;
int img_scale = 10;

int video_idx[MAX], audio_idx[MAX];
AVPacket pkt[MAX];

static int static_pts = 0;

#define filterWidth MATRIX_SIZE
#define filterHeight MATRIX_SIZE
float kernel [MATRIX_SIZE * MATRIX_SIZE];
float filter [MATRIX_SIZE * MATRIX_SIZE] =
{
    1, 1, 1,
    1, -7, 1,
    1, 1, 1
};

AVDictionary *fillupVDictionary (AVDictionary *dictionary)
{
    AVDictionary * dict = dictionary;
    av_dict_set(&dict, "tune", "zerolatency", 0);
    av_dict_set(&dict, "subq", "9", 0);
    av_dict_set(&dict, "coder", "1", 0);
    av_dict_set(&dict, "mbtree", "1", 0);
    av_dict_set(&dict, "dct8x8", "1", 0);
    av_dict_set(&dict, "partitions", "i8x8", 0);
    return dict;
}

AVCodecContext *fillupVContexts (AVCodecContext *encCtx, int width, int height)
{
    AVCodecContext * pVencCtx = encCtx;
    pVencCtx->codec_id = AV_CODEC_ID_H264;
    pVencCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pVencCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    pVencCtx->width = width;
    pVencCtx->height = height;
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
    pVencCtx->flags |= CODEC_FLAG_GLOBAL_HEADER | CODEC_FLAG_LOOP_FILTER | CODEC_FLAG_PASS1;
    return  pVencCtx;
}

int InitVideoDecoder (int i, int k )
{
    pVCodecCtx=ifmt_ctx[i]->streams[k]->codec;
    pVCodec=avcodec_find_decoder(pVCodecCtx->codec_id);

    if(pVCodec==NULL)
        { fprintf(stderr,"Could not find needed video codec\n"); return -1; }

    if( avcodec_open2(pVCodecCtx, pVCodec, NULL) < 0)
        {  fprintf(stderr,"Can not open video codec \n");  return -1; }
    else
    {  fprintf(stdout,"Be used video codec #%d \n", (int)pVCodecCtx->codec_id ); }
    AVStream *in_stream = ifmt_ctx[i]->streams[k];
    AVRational timeBase = in_stream->codec->time_base;
    //AVStream *out_stream = avformat_new_stream(ofmt_ctx[i], in_stream->codec->codec);
    return 0;
}

int InitAudioEncoder (int i, int k)
{
    //int aencID = AV_CODEC_ID_AAC;
    int aencID = ifmt_ctx[i]->streams[k]->codec->codec_id;
    pAenc=avcodec_find_encoder(aencID);
    if(NULL == pAenc)
    {
        fprintf(stderr,"Could not find needed audio encoder\n"); exit(1);
    }
    //avformat_alloc_output_context2(&ovc, NULL, out_format, out_filename); // no need 2nd time
    ovc->audio_codec_id = aencID;
    ovc->oformat->audio_codec = aencID;
    pAencCtx= avcodec_alloc_context3(pAenc);
    pAencCtx->codec_id = aencID;
    pAencCtx->codec_type = AVMEDIA_TYPE_AUDIO;

    audio_st = avformat_new_stream(ovc, pAenc);
    if (!audio_st) {
        fprintf(stderr, "Could not allocate video stream\n");
        exit(1);
    }

    audio_st->id = ovc->nb_streams;
    audio_st->time_base = ifmt_ctx[i]->streams[k]->codec->time_base;
    audio_st->codec->codec_tag = 0;
    if (ovc->oformat->flags & AVFMT_GLOBALHEADER)
        { audio_st->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;}


    fprintf(stdout," Audio encoder fillup stream #%d\n", ovc->nb_streams);
    pAencCtx = audio_st->codec;
    pAencCtx->bit_rate = ifmt_ctx[i]->streams[k]->codec->bit_rate ;

    fprintf(stderr, "pAencCtx->bit_rate %d\n", ifmt_ctx[i]->streams[k]->codec->bit_rate);
    pAencCtx->sample_rate = ifmt_ctx[i]->streams[k]->codec->sample_rate ;
    fprintf(stderr, "pAencCtx->sample_rate %d\n", ifmt_ctx[i]->streams[k]->codec->sample_rate);
    pAencCtx->channels =  ifmt_ctx[i]->streams[k]->codec->channels;
    fprintf(stderr, "pAencCtx->channels %d\n", ifmt_ctx[i]->streams[k]->codec->channels);
    pAencCtx->sample_fmt = ifmt_ctx[i]->streams[k]->codec->sample_fmt;

    fprintf(stderr, "pAencCtx->sample_fmt %d\n", ifmt_ctx[i]->streams[k]->codec->sample_fmt);
    pAencCtx->channel_layout = av_get_default_channel_layout(pAencCtx->channels);

    int data_size =  av_get_bytes_per_sample(ifmt_ctx[i]->streams[k]->codec->sample_fmt);
        //int channel_size =  ain_frame->nb_samples * sizeof(uint8_t);
        // int frame_size =  channel_size * data_size;
    pAencCtx->frame_size = 1024;

    if ( AV_CODEC_ID_AAC == ovc->oformat->audio_codec)
    {
        //pAencCtx->bit_rate = 96000;
        pAencCtx->sample_fmt = AV_SAMPLE_FMT_S16; // AV_SAMPLE_FMT_S16P for MP3
        // pAencCtx->frame_size = 1152 * data_size;
        //pAencCtx->channel_layout = AV_CH_LAYOUT_5POINT1;
        //pAencCtx->sample_rate = 48000;
        // pAencCtx->->frame_size = ;
    }
    if ( AV_CODEC_ID_MP3 == ovc->oformat->audio_codec)
    {
        pAencCtx->sample_fmt = AV_SAMPLE_FMT_S16P;
        //pAencCtx->sample_rate = 22050;
    }

    //pAencCtx->time_base = (AVRational){1, m_globalData.GetConfig().Audio().Rate()};
    //m_audioStream->time_base = pAencCtx->time_base;
    //pAencCtx = fillupVContexts (pVencCtx);

    ovc->bit_rate += pAencCtx->bit_rate;
    if (avcodec_open2(pAencCtx, pAenc, NULL) < 0) {
        fprintf(stderr, "Could not open audio encoder\n");
        exit(1);
    }
    fprintf(stdout," Audio encoder opened\n");
}
int InitVideoEncoder (int i, int k)
{
    if (ifmt_ctx[i]->streams[k]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
    {
    // Add the audio and video streams using the default format codecs and initialize the codecs.
        if (ofmte[i]->video_codec != AV_CODEC_ID_NONE) {

        pVenc = avcodec_find_encoder(AV_CODEC_ID_H264);

        if(NULL == pVenc)
        {
            fprintf(stderr,"Could not find needed video encoder\n"); exit(1);
        }

            avformat_alloc_output_context2(&ovc, NULL, out_format, out_filename);
            ovc->video_codec_id = AV_CODEC_ID_H264;
            ovc->audio_codec_id = AV_CODEC_ID_NONE;
            //context->bit_rate = m_videoOptions.bitrate * 1024;
            ovc->bit_rate = 90000;
            ovc->flags |= AVFMT_GLOBALHEADER;

            pVencCtx= avcodec_alloc_context3(pVenc);

            video_st = avformat_new_stream(ovc, pVenc);
            if (!video_st) {
                fprintf(stderr, "Could not allocate video stream\n");
                exit(1);
            }
            video_st->id = ovc->nb_streams-1;
            video_st->sample_aspect_ratio = av_d2q(1, 255);
            video_st->pts_wrap_bits = 33;

            pVencCtx = video_st->codec;
            pVencCtx = fillupVContexts (pVencCtx, ifmt_ctx[i]->streams[k]->codec->width, ifmt_ctx[i]->streams[k]->codec->height);
            AVDictionary* dict = NULL;
            dict = fillupVDictionary (dict);

            if (avcodec_open2(pVencCtx, pVenc, NULL) < 0) {
                fprintf(stderr, "Could not open video codec\n");
                exit(1);
            }

            video_st->time_base = ifmt_ctx[i]->streams[k]->codec->time_base;
            video_st->codec->codec_tag = 0;
            if (ovc->oformat->flags & AVFMT_GLOBALHEADER)
                { video_st->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;}

        }
    }
    return 0;
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
    long vCount = 0;
    long vTime = 0;

    SwrContext *resample_context = swr_alloc();

    int dx = imgw;//639;
    int dy = imgh;//480;

    int ox = 50;
	int oy = 100;

    uint8_t* rbuffer = (uint8_t *)av_malloc ( pVencCtx->height* pVencCtx->width*4 * sizeof (uint8_t));
    uint8_t* tga = (uint8_t *)av_malloc ( dx* dy * 4 * sizeof (uint8_t));
    uint8_t* blured_box = (uint8_t *)av_malloc ( dx* dy * 4 * sizeof (uint8_t));
    uint8_t* bbox = (uint8_t *)av_malloc ( dx* dy * 4 * sizeof (uint8_t));
    uint8_t* bbuffer = (uint8_t *)av_malloc ( dx* dy * 4 * sizeof (uint8_t));

    while (1)
    {
        pkt[id].data = NULL;
        pkt[id].size = 0;

        ret = av_read_frame(ifmt_ctx[id], &pkt[id]);
        if (ret < 0)
            break;

        int aindx = pkt[id].stream_index;

        if (pkt[id].stream_index == ( idxa ))
        {
            AVStream *in_stream = ifmt_ctx[id]->streams[pkt[id].stream_index];
            AVStream *out_stream = ofmt_ctx[id]->streams[pkt[id].stream_index];
            AVRational time_base = ifmt_ctx[id]->streams[idxa]->time_base;
            AVStream *enc_stream = ovc->streams[pkt[id].stream_index];

            av_opt_set_int(resample_context, "in_channel_layout",  in_stream->codec->channel_layout, 0);
            av_opt_set_int(resample_context, "out_channel_layout", enc_stream->codec->channel_layout,  0);
            av_opt_set_int(resample_context, "in_sample_rate",     in_stream->codec->sample_rate,                0);
            av_opt_set_int(resample_context, "out_sample_rate",    enc_stream->codec->sample_rate,                0);
            av_opt_set_sample_fmt(resample_context, "in_sample_fmt",  in_stream->codec->sample_fmt, 0);
            av_opt_set_sample_fmt(resample_context, "out_sample_fmt", enc_stream->codec->sample_fmt,  0);
            swr_init(resample_context);

#ifdef LIVE_STREAM         
           int time = 1000;
#else    
           int time = 1000 * 1000 * strtof(av_ts2timestr(pkt[id].duration, &time_base), NULL);           
#endif            
            usleep(time);

            int afinished = 0;
            int got_apacket = 0;
            int len;
            int adata_size = 0;
            uint64_t apts = 0;

            AVPacket oapkt ;
            AVPacket apkt ;
            oapkt.data = NULL;
            oapkt.size = 0;
            apkt.data = NULL;
            apkt.size = 0;

           // oapkt.flags |= AV_PKT_FLAG_KEY;
            //oapkt.pts = oapkt.dts = pktCount;
            av_init_packet(&oapkt);
            av_init_packet(&apkt);
            oapkt.pos = -1;
            //ovc->streams [pkt[id].stream_index]->codec->coded_frame->pts = pktCount; // seg fault

            AVFrame *ain_frame = av_frame_alloc();
            if (!(ain_frame = av_frame_alloc())) {
                fprintf(stderr, "Could not allocate audio frame\n");
                continue;
            }

            AVFrame *aout_frame = av_frame_alloc();
            if (!(aout_frame = av_frame_alloc())) {
                 fprintf(stderr, "Could not allocate audio frame\n");
                 continue;
            }

            len = avcodec_decode_audio4(in_stream->codec, ain_frame,  &afinished, &pkt[id]);
            if (len < 0) {
                fprintf(stderr, "Could not decode frame (error '%s')\n",  av_err2str(ret));
                av_free_packet(&pkt[id]);
                continue;
            }

            int data_size =  av_get_bytes_per_sample(in_stream->codec->sample_fmt);
            int channel_size =  ain_frame->nb_samples * sizeof(uint8_t);
            int frame_size = channel_size * data_size;
            int osize = av_get_bytes_per_sample(enc_stream->codec->sample_fmt);
            int frame_bytes = enc_stream->codec->frame_size * osize * enc_stream->codec->channels;
            int nb_samples = frame_bytes / (enc_stream->codec->channels * av_get_bytes_per_sample(enc_stream->codec->sample_fmt));

            enc_stream->codec->frame_size = frame_size; // fix for MP3 ain_frame

            if (data_size < 0) {
                fprintf(stderr, "Failed to calculate data size\n");
                continue;
            }

            uint8_t **converted_samples;
            converted_samples = ( uint8_t **) calloc(frame_size,sizeof(uint8_t)); //
            if ( NULL == converted_samples)
            {
                fprintf(stderr, "Could not allocate converted input sample pointers\n");
            }

            uint8_t *buf = ain_frame->data[0];
            int buf_size = ain_frame->linesize[0];
            uint8_t *audio_data_buf;

            audio_data_buf = ( uint8_t *) calloc(frame_size,sizeof(uint8_t)); //
            if ( NULL == audio_data_buf)
            {
                fprintf(stderr, "Could not allocate converted input sample pointers\n");
            }

            if (afinished) {
            //SaveAFrames (aframe, data_size);

            apts = ain_frame->pts;
            int n_out =  (int ) (ain_frame->nb_samples * ( float )in_stream->codec->sample_rate / (float) in_stream->codec->sample_rate);
            aout_frame->nb_samples = n_out;

            av_samples_alloc(converted_samples, NULL,enc_stream->codec->channels,
            aout_frame->nb_samples, enc_stream->codec->sample_fmt, 0);

           // ain_frame->extended_data = converted_samples;
           // av_frame_copy_props(aout_frame, ain_frame);
           // av_rescale_q(ain_frame->pts,  time_base, (AVRational){ 1, enc_stream->codec->sample_rate });
            ret = swr_convert(resample_context, converted_samples, aout_frame->nb_samples,
                              (const uint8_t **)ain_frame->extended_data, ain_frame->nb_samples);

            if (ret < 0) {
                fprintf(stderr, "Error swr_convert audio frame: %s\n", av_err2str(ret));
                exit(1);
            }

            ain_frame->extended_data = converted_samples;
            ain_frame->pts = pktCount; // apts

            // av_frame_unref(aout_frame);
            avcodec_fill_audio_frame(aout_frame,enc_stream->codec->channels,enc_stream->codec->sample_fmt,
            ain_frame->extended_data[0], frame_bytes, 1);

            aout_frame->pts = pktCount;
            aout_frame->linesize [0] = nb_samples;
            aout_frame->nb_samples = n_out;
            // SaveAFrames (aout_frame, av_get_bytes_per_sample(enc_stream->codec->sample_fmt));
//            if (pktCount < 5){
//                printf ( "formats: in = %d, out = %d\n", in_stream->codec->sample_fmt, enc_stream->codec->sample_fmt );
//                printf ("aout : nb_samples =%d , frame size =%d , %d \n ", aout_frame->nb_samples, frame_size, enc_stream->codec->frame_size );
//                int jj;
//                for (jj =0 ;jj <  aout_frame->nb_samples  ; jj++) {
//                    if (ain_frame->extended_data[jj] != aout_frame->extended_data[jj])
//                        printf ("@ %d = ,  ai : =%d, ao= %d\n ", jj, ain_frame->extended_data[jj], aout_frame->extended_data[jj]);
//                }
//            }

            ret = avcodec_encode_audio2(enc_stream->codec, &oapkt, aout_frame, &got_apacket); // need aout_frame
            if (ret < 0) {
                  fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
                //exit(1);
                }
            } // afinished

            if (got_apacket) {
                int pkthi ;
                int pktho ;
               //av_copy_packet (&oapkt, &pkt[id]);
               // memcpy (oapkt.data,apkt.data, oapkt.size );
               // memcpy (oapkt.data,pkt[id].data, 4 );
                memcpy (&pktho,oapkt.data, 4 );
                memcpy (&pkthi,pkt[id].data, 4 );
                oapkt.duration = pkt[id].duration;
                oapkt.pos = pkt[id].pos;
                oapkt.pts = pkt[id].pts;
                oapkt.dts = pkt[id].dts;
                oapkt.stream_index = pkt[id].stream_index;

                if ((pktho & 0xffff) != 0xfbff)
                {
                    // printf ("audio copy done pkthi = %x, pktho = %x \n", pkthi , pktho);
                    // printf   ("pktCount %d\n", pktCount );
                    printf ("~");
                }
                else
                    printf (".");

                //rescaleTimeBase(&oapkt, &pkt[id], in_stream->time_base,enc_stream->time_base );
                ret = av_interleaved_write_frame(ovc,&oapkt);
                //ret = av_interleaved_write_frame(ovc, &pkt[id]);
                av_frame_free (&ain_frame);
                av_frame_free (&aout_frame);
                av_free_packet(&oapkt);
                av_free_packet(&apkt);
                if (ret < 0) {
                   fprintf(stderr, "Error while writing audio frame: %s\n", av_err2str(ret));
                }
            } // got_apacket
            else
                printf (",");
           //av_freep(&converted_samples[0]);

//            pkt[id].pts = av_rescale_q_rnd(pkt[id].pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
//            pkt[id].dts = av_rescale_q_rnd(pkt[id].dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
//            pkt[id].duration = av_rescale_q(pkt[id].duration, in_stream->time_base, out_stream->time_base);
//            pkt[id].pos = -1;
//            pkt[id].stream_index = idxa;

#ifdef COPY_APACKETS
#ifdef COPY_VPACKETS
           ret = av_interleaved_write_frame(ofmt_ctx[id], &pkt[id]);
#else
          // ret = av_interleaved_write_frame(ovc, &pkt[id]);

#endif
          //ret = av_interleaved_write_frame(ofmt_ctx[id], &pkt[id]);
#else
           ret = av_interleaved_write_frame(ovc, &pkt[id]);
#endif
            if (ret < 0)
            {
                fprintf(stderr, "Error muxing packet thread %d\n", id);
                break;
            }     
        
            av_free_packet(&pkt[id]);
            pktCount++;
            continue;
        }

        // video packets handler
        if (pkt[id].stream_index != idx) {
            continue;
        }

        AVStream *in_stream = ifmt_ctx[id]->streams[pkt[id].stream_index];
        AVStream *out_stream = ofmt_ctx[id]->streams[pkt[id].stream_index];
        AVRational time_base = ifmt_ctx[id]->streams[idx]->time_base;
        AVStream *enc_stream = ovc->streams[pkt[id].stream_index];

        int ih = in_stream->codec->height;
        int iw = in_stream->codec->width;
        int ph = in_stream->codec->height;
        int pw = in_stream->codec->width;
        int oh = in_stream->codec->height;
        int ow = in_stream->codec->width;
        int got_packet = 0;
        int frameFinished;

        AVFrame *pinFrame;
        AVFrame *pFrameRGB;
        AVFrame *poutFrame;
        struct SwsContext *img_convert_ctxi = NULL;
        struct SwsContext *img_convert_ctxo = NULL;

        //AV_PIX_FMT_YUV420P AV_PIX_FMT_RGB24 AV_PIX_FMT_BGRA AV_PIX_FMT_YUVJ420P

        enum AVPixelFormat oPixFormat = AV_PIX_FMT_YUV420P;
        enum AVPixelFormat pPixFormat = AV_PIX_FMT_RGBA; //
        enum AVPixelFormat iPixFormat = in_stream->codec->pix_fmt;

        pinFrame=av_frame_alloc(); // Allocate video frame
        pFrameRGB=av_frame_alloc();
        poutFrame=av_frame_alloc();

//        parser = av_parser_init(AV_CODEC_ID_H264);
//        if(!parser) {
//            fprintf(stderr,"Erorr: cannot create H264 parser.\n");
//            exit (-1);
//        }

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

         sws_scale(img_convert_ctxi, (const uint8_t * const*) pinFrame->data, pinFrame->linesize, 0, ih , pFrameRGB->data, pFrameRGB->linesize);

         long time ;

         time = get_time_ms ();
         crop (bufferRGB, pw,ph, tga,ox,oy,dx,dy,bytesPerPixel);
         int bscale = 8;
         int dys = dy/bscale;
         int dxs = dx/bscale;

         // blur over resize
         stbir_resize_uint8(tga, dx, dy, 0, bbox, dxs, dys, 0, bytesPerPixel);
         stbir_resize_uint8(bbox, dxs, dys, 0, blured_box, dx, dy, 0, bytesPerPixel);
         img_filter ( FILL_BY_1S, kernel ,MATRIX_SIZE, dx,dy, 0, 0, bytesPerPixel,  blured_box ,tga);
         //img_filter ( TRICK_COPY, kernel ,MATRIX_SIZE, dx,dy, 0, 0, bytesPerPixel,  blured_box ,tga);
         //img_filter ( TRICK_OFF, kernel ,MATRIX_SIZE, dx,dy, 0, 0, bytesPerPixel,  blured_box ,tga);

         // filter
         //memcpy (blured_box, tga, dx* dy * 4 * sizeof (uint8_t));
         //img_filter (FILL_BY_1S, kernel ,MATRIX_SIZE, dx,dy, 0, 0, bytesPerPixel,  blured_box ,tga);

         if ( 1 == frameCount){
             stbi_write_tga("in.tga", dx, dy, bytesPerPixel ,tga);
             stbi_write_tga("bbox.tga", dxs, dys, bytesPerPixel ,bbox);
             stbi_write_tga("out.tga", dx, dy, bytesPerPixel ,blured_box);
         }
//         img_filter ( kernel ,MATRIX_SIZE,pw,ph, 0, 0, 4, bufferRGB, rbuffer);

         overlay (bufferRGB, pw,ph, blured_box,ox,oy,dx,dy,bytesPerPixel);
         long cTime = get_time_ms() - time;
         vTime += cTime;
         vCount++;
         printf ("spent  = %ld avg = %ld ms\n", cTime , vTime / vCount );

        // vec4* stack = new vec4[ div ];
        // gdImagePtr gdst = gdImageCreate(640, 480);

        // change bits in pix map
         int jj ;
         int ll = 0;
         int sox = ox ;
         int eoy = oy ;
         //imgh = dy;
         //imgw = dx;
//
//         for (jj = 0 ; jj < pw * imgh ; jj++)
//         {
//             if (0 == (jj * bytesPerPixel ) % ( pw * bytesPerPixel) )
//             {
//                 int kk;
//                 for (kk = sox; kk <  (sox + imgw) * bytesPerPixel ; kk++ )
//                 {
//                     if ( LEVEL_BLACK != pix_buffer [ ll * imgw *bytesPerPixel+ kk] && LEVEL_WHITE != pix_buffer [ ll * imgw *bytesPerPixel+ kk])
//                        bufferRGB [ll * pw *bytesPerPixel + kk] = pix_buffer [ ll * imgw *bytesPerPixel+ kk];
//                     //if ( LEVEL_BLACK != blured_box [ ll * imgw *bytesPerPixel+ kk] && LEVEL_WHITE != blured_box [ ll * imgw *bytesPerPixel+ kk])
//                     //    bufferRGB [ll * pw *bytesPerPixel + kk] = blured_box [ ll * imgw *bytesPerPixel+ kk];
//                 }
//                 ll++;
//             }
//         }

         poutFrame->height = 480;
         poutFrame->width = 640;
         poutFrame->format = (int) oPixFormat;

         if(img_convert_ctxo == NULL)
             img_convert_ctxo = sws_getContext(pw, ph, pPixFormat, ow, oh,oPixFormat,SWS_BICUBIC, NULL, NULL, NULL);
         sws_scale(img_convert_ctxo, (const uint8_t * const*) pFrameRGB->data, pFrameRGB->linesize, 0, ph , poutFrame->data, poutFrame->linesize);

         //pgm_save(bufferRGB, poutFrame->linesize[0],ow,oh, frameCount+1);
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

        if(frameFinished)
        {

            AVPacket opkt ;
            opkt.data = NULL;
            opkt.size = 0;
            opkt.flags |= AV_PKT_FLAG_KEY;
            opkt.pts = opkt.dts = pktCount;
            av_init_packet(&opkt);
            opkt.pos = -1;
            ovc->streams [pkt[id].stream_index]->codec->coded_frame->pts = pktCount;

           poutFrame->pts =  pktCount;
           av_frame_get_best_effort_timestamp(poutFrame);
           ret = avcodec_encode_video2( ovc->streams [pkt[id].stream_index]->codec, &opkt, poutFrame, &got_packet);

           if (ret < 0)  {
                fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
           }

           if (got_packet)
           {
               if(ovc->streams [pkt[id].stream_index]->codec->coded_frame->key_frame)
               {
                   opkt.flags |= AV_PKT_FLAG_KEY;
               }
               rescaleTimeBase(&opkt, &pkt[id], in_stream->time_base,enc_stream->time_base );

#ifndef COPY_VPACKETS
               ret = av_interleaved_write_frame(ovc, &opkt);
               if (ret < 0)
               {
                   fprintf(stderr, "Error writing video frame: %s\n", av_err2str(ret));
               }
               av_free_packet(&opkt);
#endif
           }

#ifdef COPY_VPACKETS
           ret = av_interleaved_write_frame(ofmt_ctx[id], &pkt[id]);

#else
#endif
       } // frameFinished

        av_free_packet(&pkt[id]);
        av_frame_free(&pinFrame);
        av_frame_free(&pFrameRGB);
        av_frame_free(&poutFrame);
        av_free (bufferRGB);
        av_free (in_buffer);
        av_free (out_buffer);
       pktCount++;

        if (ret < 0)
        {
            fprintf(stderr, "Error muxing packet thread %d\n", id);
            fprintf(stderr, "write frame result: %s\n", av_err2str(ret));
            break;
        }
    }
}

int main(int argc, char **argv)
{
    unsigned char tga_header [TGA_HEADER_SIZE] = {0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,32,0 };
    fill_TGA_header(tga_header, 2, 640, 480, 1);
    float factor;
    do_kernel(MATRIX_SIZE, kernel, FILL_BY_1S, &factor );
    //memcpy (kernel, filter, MATRIX_SIZE * MATRIX_SIZE * (sizeof(float)));

    int ret, i = 0;
    static int video_stream_idx = -1, audio_stream_idx = -1;

    memset(ofmt, 0, sizeof(ofmt));
    memset(ofmt_ctx, 0, sizeof(ofmt_ctx));
    memset(ofmte, 0, sizeof(ofmte));
    memset(ofmte_ctx, 0, sizeof(ofmte_ctx));
    memset(ifmt_ctx, 0, sizeof(ifmt_ctx));
    memset(video_idx, -1, sizeof(video_idx));

    char* img_infile = "logo.jpg"; // "logo.jpg" "lena.jpeg"

    unsigned char* pixeldata = stbi_load(img_infile, &imgw, &imgh, &bytesPerPixel, 4);
    //int pix_buffer_size = imgw * imgh * bytesPerPixel;
    //pix_buffer = malloc (pix_buffer_size);
    //memcpy (pix_buffer, pixeldata, pix_buffer_size);
    // if you have already read the image file data into a buffer:
  //  unsigned char* pixeldata2 = stbi_load_from_memory(bufferWithImageData, bufferLength, &width, &height, &bytesPerPixel, 0);
    if(pixeldata == NULL) {
        printf("Some error happened: %sn", stbi_failure_reason());
        exit (-1);
    }
    else
        printf("loaded %s: w = %d , h = %d , b = %d\n", img_infile, imgw, imgh, bytesPerPixel);

    bytesPerPixel = 4;
    int out_w = imgw/img_scale;
    int out_h = imgh/img_scale;
    int pix_buffer_size = out_w*out_h*bytesPerPixel;
    pix_buffer = (unsigned char*) malloc(pix_buffer_size);
    stbir_resize_uint8(pixeldata, imgw, imgh, 0, pix_buffer, out_w, out_h, 0, bytesPerPixel);
    imgw = out_w ;
    imgh = out_h ;
    printf("used image %s: w = %d , h = %d , b = %d\n", img_infile, imgw, imgh, bytesPerPixel);
    //pix_buffer_size = pix_buffer_osize;
    //memcpy (pix_buffer, pixeldata, pix_buffer_size);

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
                if (ofmte[i]->video_codec != AV_CODEC_ID_NONE) {
                    InitVideoEncoder (i,k);
              }
             InitVideoDecoder(i,k);

             AVStream *in_stream = ifmt_ctx[i]->streams[k];
             AVRational timeBase = in_stream->codec->time_base;
             AVStream *out_stream = avformat_new_stream(ofmt_ctx[i], in_stream->codec->codec);

#ifdef COPY_VPACKETS
             ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
             if (ret < 0)
             {
                 fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
                 goto end;
             }

             out_stream->time_base = timeBase;
             out_stream->codec->codec_tag = 0;
             if (ofmt_ctx[i]->oformat->flags & AVFMT_GLOBALHEADER)
                { out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;}
#endif

                video_idx[i] = k;
            }

            if (ifmt_ctx[i]->streams[k]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                if (ofmte[i]->audio_codec != AV_CODEC_ID_NONE) {
#ifdef COPY_APACKETS
                     InitAudioEncoder (i,k);
#endif
                 } // audio encoder init

                 // audio decoder init
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

                if (!out_stream)
                {
                    fprintf(stderr, "Failed allocating output stream\n");
                    ret = AVERROR_UNKNOWN;
                    goto end;
                }

#ifndef COPY_APACKETS
                audio_st = avformat_new_stream(ovc, ina_stream->codec->codec);
                ret = avcodec_copy_context(audio_st->codec, ina_stream->codec);
                if (ret < 0)
                {
                    fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
                    goto end;
                }
                audio_st->time_base = ifmt_ctx[i]->streams[k]->codec->time_base;
                audio_st->codec->codec_tag = 0;
                if (ovc->oformat->flags & AVFMT_GLOBALHEADER)
                    { audio_st->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;}
#endif

                ret = avcodec_copy_context(out_stream->codec, ina_stream->codec);
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

                audio_idx[i] = k;
                fprintf(stdout, "Output audio @stream #%d \n",audio_idx[i+1]);
            }
        } // for

         if (!(ofmt[i]->flags & AVFMT_NOFILE))
        {
#ifdef COPY_VPACKETS
            ret = avio_open(&ofmt_ctx[i]->pb, out_filename, AVIO_FLAG_WRITE);
#else
            ret = avio_open(&ovc->pb, out_filename, AVIO_FLAG_WRITE);
#endif
            if (ret < 0)
            {
                fprintf(stderr, "Could not open output file '%s'", out_filename);
                goto end;
            }
        }

        fprintf(stdout, "Dump output format\n");
#ifdef  COPY_VPACKETS
        ret = avformat_write_header(ofmt_ctx[i], NULL);
        av_dump_format(ofmt_ctx[i], 0, out_filename, 1);
#else
        ret = avformat_write_header(ovc, NULL);
        av_dump_format(ovc, 0,  out_filename, 1);
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
#ifdef COPY_VPACKETS
        av_write_trailer(ofmt_ctx[i]);
#else
        av_write_trailer(ovc);
#endif
        avformat_close_input(&ifmt_ctx[i]);
#ifdef COPY_VPACKETS
        if (ofmt_ctx[i] && !(ofmt[i]->flags & AVFMT_NOFILE))
            avio_closep(&ofmt_ctx[i]->pb);
        avformat_free_context(ofmt_ctx[i]);
#else
        if (ovc && !(ofmte[i]->flags & AVFMT_NOFILE))
           avio_closep(&ovc->pb);
        avformat_free_context(ovc);
#endif

        if (ret < 0 && ret != AVERROR_EOF)
        {
            fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
            return 1;
        }
    }
    return 0;
}
