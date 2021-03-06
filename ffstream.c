#include <pthread.h>
#include <unistd.h>

#include "utils.h"
#include "filter.h"
#include "stb_defs.h"

#include "avheader.h"
//#include "jsmn/jsmn.h"

#include <json-c/json.h>
//#include <json-c/parse_flags.h>

#define MAX 1
#define LIVE_STREAM 
//#define COPY_VPACKETS
#define COPY_APACKETS

#define MATRIX_SIZE 3
#define KERNEL_HALF_SIZE ((MATRIX_SIZE * MATRIX_SIZE + 1)/2)

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
volatile uint8_t * pix_buffer;
int imgh;
int imgw;
int bytesPerPixel;
int img_scale = 10;

int video_idx[MAX], audio_idx[MAX];
AVPacket pkt[MAX];

static int static_pts = 0;

int parse_json (void)
{
    json_object *new_obj;
    json_object *jbody;
    const char *jsb = "blur";
    const char *js = "{ \"blur\":[{\"id\":1, \"ox\":10, \"oy\":10 ,  \"w\": 160, \"h\":90}, \
                                {\"id\":2, \"ox\":10, \"oy\":100 , \"w\": 160, \"h\":90}, \
                                {\"id\":3, \"ox\":10, \"oy\":200 , \"w\": 160, \"h\":90}  ]}";
    //const char *js  = "{\"server\": \"example.com\", \"post\": 80,  \"message\": \"hello world\"}";
    new_obj = json_tokener_parse(js);
    printf("new_obj.to_string()=%s\n", json_object_to_json_string(new_obj));
    new_obj = json_tokener_parse(js);
       enum json_type type = json_object_get_type(new_obj);
       printf("type: %d \n",type);
       switch (type) {
       case json_type_null: printf("json_type_null\n");
       break;
       case json_type_boolean: printf("json_type_boolean\n");
       break;
       case json_type_double: printf("json_type_double\n");
       break;
       case json_type_int: printf("json_type_int\n");
       break;
       case json_type_object: printf("json_type_object\n");
             if ( json_object_object_get_ex(new_obj, "blur", &jbody))
                 printf("jbody = %s\n", json_object_to_json_string(jbody));
             else
                 printf("jbody = %s\n", "!empty");
       break;
       case json_type_array: printf("json_type_array\n");
       break;
       case json_type_string: printf("json_type_string\n");
       break;
       }

    json_object_put(new_obj);
    return 0;
};

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
#ifdef __cplusplus
    AVCodecID aencID;
#else
    int aencID;
#endif
    aencID = ifmt_ctx[i]->streams[k]->codec->codec_id;
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
	int oy = 10;

    uint8_t* rbuffer = (uint8_t *)av_malloc ( pVencCtx->height* pVencCtx->width*bytesPerPixel * sizeof (uint8_t));
    volatile uint8_t* tga0 = (uint8_t *)av_malloc ( dx* dy * bytesPerPixel * sizeof (uint8_t));
    volatile uint8_t* tga1 = (uint8_t *)av_malloc ( dx* dy * bytesPerPixel * sizeof (uint8_t));
    volatile uint8_t* tga2 = (uint8_t *)av_malloc ( dx* dy * bytesPerPixel * sizeof (uint8_t));
    volatile uint8_t* blured_box = (uint8_t *)av_malloc ( dx* dy * bytesPerPixel * sizeof (uint8_t));
    volatile uint8_t* blured_box2 = (uint8_t *)av_malloc ( dx* dy * bytesPerPixel * sizeof (uint8_t));
    volatile uint8_t* bbox = (uint8_t *)av_malloc ( dx* dy * bytesPerPixel * sizeof (uint8_t));
    volatile uint8_t* bbox2 = (uint8_t *)av_malloc ( dx* dy * bytesPerPixel * sizeof (uint8_t));
    uint8_t* bbuffer = (uint8_t *)av_malloc ( dx* dy * bytesPerPixel * sizeof (uint8_t));

    AVFrame *ain_frame ;
    if (!(ain_frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate audio frame\n");
    }

    AVFrame *aout_frame ;
    if (!(aout_frame = av_frame_alloc())) {
         fprintf(stderr, "Could not allocate audio frame\n");
    }

    AVStream *in_astream = ifmt_ctx[id]->streams[idxa];
    AVStream *in_vstream = ifmt_ctx[id]->streams[idx];

    int ih = in_vstream->codec->height;
    int iw = in_vstream->codec->width;
    int ph = in_vstream->codec->height;
    int pw = in_vstream->codec->width;
    int oh = in_vstream->codec->height;
    int ow = in_vstream->codec->width;

    //AV_PIX_FMT_YUV420P AV_PIX_FMT_RGB24 AV_PIX_FMT_BGRA AV_PIX_FMT_YUVJ420P
    enum AVPixelFormat iPixFormat = in_vstream->codec->pix_fmt;
    enum AVPixelFormat oPixFormat = AV_PIX_FMT_YUV420P;
    enum AVPixelFormat pPixFormat = AV_PIX_FMT_RGBA;

    AVFrame * pinFrame;
    if (!(pinFrame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate audio frame\n");
    }
    int num_inBytes=avpicture_get_size(iPixFormat, iw, ih);
    uint8_t* in_buffer=(uint8_t *)av_malloc(num_inBytes*sizeof(uint8_t));

    AVFrame * pFrameRGB;
    if (!(pFrameRGB = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate audio frame\n");
    }
    int num_BytesRGB=avpicture_get_size(pPixFormat, pw, ph);
    volatile uint8_t* bufferRGB=(uint8_t *)av_malloc(num_BytesRGB*sizeof(uint8_t));

    AVFrame * poutFrame;
    if (!(poutFrame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate audio frame\n");
    }
    int num_outBytes=avpicture_get_size(oPixFormat, ow, oh);
    uint8_t* out_buffer=(uint8_t *)av_malloc(num_outBytes*sizeof(uint8_t));

    struct SwsContext *img_convert_ctxi = NULL;
    struct SwsContext *img_convert_ctxo = NULL;

    av_init_packet (&pkt[id]);

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
            AVStream *out_stream = ofmt_ctx[id]->streams[pkt[id].stream_index];
            AVRational time_base = ifmt_ctx[id]->streams[idxa]->time_base;
            AVStream *enc_stream = ovc->streams[pkt[id].stream_index];

            av_opt_set_int(resample_context, "in_channel_layout",  in_astream->codec->channel_layout, 0);
            av_opt_set_int(resample_context, "out_channel_layout", enc_stream->codec->channel_layout,  0);
            av_opt_set_int(resample_context, "in_sample_rate",     in_astream->codec->sample_rate,                0);
            av_opt_set_int(resample_context, "out_sample_rate",    enc_stream->codec->sample_rate,                0);
            av_opt_set_sample_fmt(resample_context, "in_sample_fmt",  in_astream->codec->sample_fmt, 0);
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

            av_init_packet(&oapkt);
            av_init_packet(&apkt);
            oapkt.pos = -1;

            len = avcodec_decode_audio4(in_astream->codec, ain_frame,  &afinished, &pkt[id]);
            if (len < 0) {
#ifndef  __cplusplus
                fprintf(stderr, "Could not decode frame (error '%s')\n",  av_err2str(ret));
#endif
                av_free_packet(&pkt[id]);
                continue;
            }

            int data_size =  av_get_bytes_per_sample(in_astream->codec->sample_fmt);
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
            int n_out =  (int ) (ain_frame->nb_samples * ( float )in_astream->codec->sample_rate / (float) in_astream->codec->sample_rate);
            aout_frame->nb_samples = n_out;

            av_samples_alloc(converted_samples, NULL,enc_stream->codec->channels,
            aout_frame->nb_samples, enc_stream->codec->sample_fmt, 0);

           // ain_frame->extended_data = converted_samples;
           // av_frame_copy_props(aout_frame, ain_frame);
           // av_rescale_q(ain_frame->pts,  time_base, (AVRational){ 1, enc_stream->codec->sample_rate });
            ret = swr_convert(resample_context, converted_samples, aout_frame->nb_samples,
                              (const uint8_t **)ain_frame->extended_data, ain_frame->nb_samples);

            if (ret < 0) {
#ifndef  __cplusplus
                fprintf(stderr, "Error swr_convert audio frame: %s\n", av_err2str(ret));
#endif
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
#ifndef  __cplusplus
                  fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
#endif
                //exit(1);
                }
            } // afinished
            av_freep (&converted_samples[0]);
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
              //  av_frame_free (&ain_frame);
              //  av_frame_free (&aout_frame);
                av_frame_unref(ain_frame);
                av_frame_unref(aout_frame);
                av_free_packet(&oapkt);
                av_free_packet(&apkt);
                if (ret < 0) {
#ifndef  __cplusplus
                   fprintf(stderr, "Error while writing audio frame: %s\n", av_err2str(ret));
#endif
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

        // no video nor audio
        if (pkt[id].stream_index != idx) {
            av_free_packet(&pkt[id]);
            continue;
        }

        // video packets handler
        if (pkt[id].stream_index == idx) {

        AVStream *out_vstream = ofmt_ctx[id]->streams[pkt[id].stream_index];
        AVRational time_base = ifmt_ctx[id]->streams[idx]->time_base;
        AVStream *enc_stream = ovc->streams[pkt[id].stream_index];

        int got_packet = 0;
        int frameFinished;

        avcodec_decode_video2(in_vstream->codec, pinFrame, &frameFinished, &pkt[id]);
        if(frameFinished)
        {
             avpicture_fill((AVPicture *)pFrameRGB, (uint8_t *) bufferRGB,pPixFormat, pw, ph);
             if(img_convert_ctxi == NULL)
                 img_convert_ctxi = sws_getContext(iw, ih, iPixFormat, pw, ph,pPixFormat,SWS_BICUBIC, NULL, NULL, NULL);

             sws_scale(img_convert_ctxi, (const uint8_t * const*) pinFrame->data, pinFrame->linesize, 0, ih , pFrameRGB->data, pFrameRGB->linesize);

             long time ;
             time = get_time_ms ();

             int bscale = 8;
             int dys = dy/bscale;
             int dxs = dx/bscale;
             int jj ;
             int ll = 0;

             int oy1 = oy + 100;
             int oy2 = oy + 200;
             struct gfilter fg1;
             fg1.bpp = bytesPerPixel;
             fg1.ox = ox;
             fg1.oy = oy;
             fg1.w = dx;
             fg1.h = dy;
             fg1.scale;

             crop (pFrameRGB->data[0], pw,ph, tga0,fg1.ox,fg1.oy,fg1.w,fg1.h,fg1.bpp);
             crop (pFrameRGB->data[0], pw,ph, tga0,ox,oy,dx,dy,bytesPerPixel);
             crop (pFrameRGB->data[0], pw,ph, tga1,ox,oy1,dx,dy,bytesPerPixel);
             crop (pFrameRGB->data[0], pw,ph, tga2,ox,oy2,dx,dy,bytesPerPixel);

#ifdef FILTER_SIMPLE_BLUR
             smooth ( (uint8_t*)tga0,(uint8_t*) blured_box ,imgw, imgh, bscale , bytesPerPixel);
             img_filter ( FILL_BY_1S, kernel ,MATRIX_SIZE, dx,dy, 0, 0, bytesPerPixel,  tga0 ,(uint8_t* )blured_box);
#else
             memcpy (blured_box, (uint8_t*)tga, dx* dy * 4 * sizeof (uint8_t));
             img_filter ( FILL_BY_1S, kernel ,MATRIX_SIZE, dx,dy, 0, 0, bytesPerPixel,  blured_box ,tga);
#endif
             overlay (pFrameRGB->data[0], pw,ph, tga0,ox,oy,dx,dy,bytesPerPixel, WITHOUT_BW_LEVELS);

             smooth ( (uint8_t*) tga1, (uint8_t*) bbox,imgw, imgh, bscale , bytesPerPixel);
             img_filter ( FILL_BY_1S, kernel ,MATRIX_SIZE, dx,dy, 0, 0, bytesPerPixel,  tga1 ,(uint8_t* )bbox2);
             overlay (pFrameRGB->data[0], pw,ph, tga1,ox,oy1,dx,dy,bytesPerPixel, WITHOUT_BW_LEVELS);

             smooth ( (uint8_t*) tga2, (uint8_t*) bbox2,imgw, imgh, bscale , bytesPerPixel);
             img_filter ( FILL_BY_1S, kernel ,MATRIX_SIZE, dx,dy, 0, 0, bytesPerPixel,  tga2 ,(uint8_t* )bbox);
             overlay (pFrameRGB->data[0], pw,ph, tga2,ox,oy2,dx,dy,bytesPerPixel, WITHOUT_BW_LEVELS);

             //add logo
             img_filter ( TRICK_COPY, kernel ,MATRIX_SIZE, dx,dy, 0, 0, bytesPerPixel,  pix_buffer ,bbuffer);
             overlay (pFrameRGB->data[0], pw,ph, pix_buffer,ox,oy,imgw,imgh,bytesPerPixel,WITH_BW_LEVELS);

            //img_filter ( FILL_BY_1S, kernel ,MATRIX_SIZE,pw,ph, 0, 0, 4, bufferRGB, rbuffer);

             long cTime = get_time_ms() - time;
             vTime += cTime;
             vCount++;
             //printf ("spent  = %ld avg = %ld ms\n", cTime , vTime / vCount );

             poutFrame->height = pw;
             poutFrame->width = ph;
             poutFrame->format = (int) oPixFormat;
             avpicture_fill((AVPicture *)poutFrame, out_buffer,oPixFormat, ow, oh);
             if(img_convert_ctxo == NULL)
                 img_convert_ctxo = sws_getContext(pw, ph, pPixFormat, ow, oh,oPixFormat,SWS_BICUBIC, NULL, NULL, NULL);
             sws_scale(img_convert_ctxo, (const uint8_t * const*) pFrameRGB->data, pFrameRGB->linesize, 0, ph , poutFrame->data, poutFrame->linesize);

            // sws_freeContext(img_convert_ctxi);
            // sws_freeContext(img_convert_ctxo);

         //if ( 1 == frameCount )
             if ( kbhit ())
             {
                 pgm_save(pinFrame->data[0], pinFrame->linesize[0],iw,ih, frameCount);
                 pgm_save(poutFrame->data[0], poutFrame->linesize[0],ow,oh, frameCount+1);
                 // pgm_save(bufferRGB, poutFrame->linesize[0],ow,oh, frameCount+1);
                 if (AV_PIX_FMT_RGB24 == pPixFormat)
                     SaveVFrameRGB (pFrameRGB, pw, ph, frameCount);
                 if (AV_PIX_FMT_RGBA == pPixFormat)
                 {
                     char filename[32];
                     sprintf(filename, "frame%d.tga", frameCount);
                     stbi_write_tga(filename, pw, ph, bytesPerPixel ,pFrameRGB->data[0]);
                 }
                 stbi_write_tga("crop.tga", dx, dy, bytesPerPixel ,(uint8_t*) tga1);
                 stbi_write_tga("cbox.tga", dxs, dys, bytesPerPixel ,(uint8_t*) bbox);
                 stbi_write_tga("cout.tga", dx, dy, bytesPerPixel ,(uint8_t*)blured_box);
             }
             frameCount++;

             av_frame_unref(pinFrame);
             av_frame_unref(pFrameRGB);
        } // frame finished after video decode

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
            av_frame_unref(poutFrame);

            if (ret < 0)  {
#ifndef  __cplusplus
                 fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
#endif
            }

            if (got_packet)
            {
                if(ovc->streams [pkt[id].stream_index]->codec->coded_frame->key_frame)
                {
                    opkt.flags |= AV_PKT_FLAG_KEY;
                }
                rescaleTimeBase(&opkt, &pkt[id], in_vstream->time_base,enc_stream->time_base );

#ifndef COPY_VPACKETS
               ret = av_interleaved_write_frame(ovc, &opkt);
               if (ret < 0)
               {
#ifndef  __cplusplus
                   fprintf(stderr, "Error writing video frame: %s\n", av_err2str(ret));
#endif
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
        av_freep(pkt);
        pktCount++;

        if (ret < 0)
        {
            fprintf(stderr, "Error muxing packet thread %d\n", id);
#ifndef  __cplusplus
            fprintf(stderr, "write frame result: %s\n", av_err2str(ret));
#endif
            break;
        }
        }// video packet
    }
}

int main(int argc, char **argv)
{
    parse_json();
    unsigned char tga_header [TGA_HEADER_SIZE] = {0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,32,0 };
    fill_TGA_header(tga_header, 2, 640, 480, 1);
    float factor;
#ifdef FILTER_SIMPLE_BLUR
    do_kernel(MATRIX_SIZE, kernel, FILL_BY_1S, &factor );
#else
    memcpy (kernel, filter, MATRIX_SIZE * MATRIX_SIZE * (sizeof(float)));
#endif
    int ret, i = 0;
    static int video_stream_idx = -1, audio_stream_idx = -1;

    memset(ofmt, 0, sizeof(ofmt));
    memset(ofmt_ctx, 0, sizeof(ofmt_ctx));
    memset(ofmte, 0, sizeof(ofmte));
    memset(ofmte_ctx, 0, sizeof(ofmte_ctx));
    memset(ifmt_ctx, 0, sizeof(ifmt_ctx));
    memset(video_idx, -1, sizeof(video_idx));

    const char* img_infile = "logo.jpg"; // "logo.jpg" "lena.jpeg"

    unsigned char* pixeldata = stbi_load(img_infile, &imgw, &imgh, &bytesPerPixel, 4);
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
    pix_buffer = (uint8_t *)av_malloc(pix_buffer_size);
    stbir_resize_uint8(pixeldata, imgw, imgh, 0, (uint8_t *)pix_buffer, out_w, out_h, 0, bytesPerPixel);

    imgw = out_w ;
    imgh = out_h ;
    printf("used image %s: w = %d , h = %d , b = %d\n", img_infile, imgw, imgh, bytesPerPixel);
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
#ifndef  __cplusplus
            fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
#endif
            return 1;
        }
    }
    return 0;
}
