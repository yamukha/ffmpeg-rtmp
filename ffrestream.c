#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <pthread.h>
#include <unistd.h>
#include "utils.h"

#define MAX 1
#define LIVE_STREAM
#define MULTIPLIER 10000
#define A_DELAY 10000
#define V_DELAY 10000
#define TIME100MS 100
#define MAX_BUFF_SIZE  65535

AVOutputFormat *ofmt[MAX];
AVFormatContext *ifmt_ctx[MAX], *ofmt_ctx[MAX];
int video_idx[MAX], audio_idx[MAX];
AVPacket pkt[MAX];
static int adelay = A_DELAY;
static int vdelay = V_DELAY;
static int apackets_nb = 0;
static int vpackets_nb = 0;
int time_100ms = 100;
int quants = 3;
typedef struct fifonode {
    //AVPacket *fn_data;
    void *fn_data;
    struct fifonode *fn_next;
} fifonode_t;

struct fifo {
    fifonode_t *f_head;
    fifonode_t *f_tail;
};

typedef struct fifo fifo_t;

fifo_t * fifo_new(void)
{
    fifo_t *f;
    f = calloc(1,sizeof (fifonode_t));
    return (f);
}

// Add to the end of the fifo
void fifo_add(fifo_t *f, void *data)
{
    fifonode_t *fn = malloc(sizeof (fifonode_t));
    fn->fn_data = data;
    fn->fn_next = NULL;

    if (f->f_tail == NULL)
        f->f_head = f->f_tail = fn;
    else {
        f->f_tail->fn_next = fn;
        f->f_tail = fn;
}
}

// Remove from the front of the fifo
void * fifo_remove(fifo_t *f)
{
    fifonode_t *fn;
    void *data;

    if ((fn = f->f_head) == NULL)
        return (NULL);

    data = fn->fn_data;
    if ((f->f_head = fn->fn_next) == NULL)
        f->f_tail = NULL;

    free(fn);
    return (data);
}

static void
fifo_nullfree(void *arg)
{
    // this function intentionally left blank
}

// Free an entire fifo
void fifo_free(fifo_t *f, void (*freefn)(void *))
{
    fifonode_t *fn = f->f_head;
    fifonode_t *tmp;

    if (freefn == NULL)
        freefn = fifo_nullfree;

    while (fn) {
        (*freefn)(fn->fn_data);

        tmp = fn;
        fn = fn->fn_next;
        free(tmp);
    }

    free(f);
}

int fifo_len(fifo_t *f)
{
    int i = 0;
    fifonode_t *fn;
    for (i = 0, fn = f->f_head; fn; fn = fn->fn_next, i++);
    return (i);
}

int fifo_empty(fifo_t *f)
{
    return (f->f_head == NULL);
}

int fifo_iter(fifo_t *f, int (*iter)(void *data, void *arg), void *arg)
{
    fifonode_t *fn;
    int rc;
    int ret = 0;

    for (fn = f->f_head; fn; fn = fn->fn_next) {
        if ((rc = iter(fn->fn_data, arg)) < 0)
            return (-1);
        ret += rc;
    }

    return (ret);
}

void list_print_head (fifo_t *p )
{
    fifonode_t *fn;
    fn = p->f_head;
    if (!fn)
        return;
    else{
      printf("Num = %d\n", *(int *)fn->fn_data);
    }
    return;
  }

void * get_tail (fifo_t *p )
{
    fifonode_t *fn;
    fn = p->f_tail;
    if (!fn)
        return;
    else{
     // printf("Num = %d\n", *(int *)fn->fn_data);
    }
    return fn->fn_data;
  }

void * get_head (fifo_t *p )
{
    fifonode_t *fn;
    fn = p->f_head;
    if (!fn)
    	return;
    else{
     // printf("Num = %d\n", *(int *)fn->fn_data);
    }
    return fn->fn_data;
}

static void print_usage() 
{
  fprintf (stdout,
           "demuxes media input to rpmt streams\n"
           "usage:  ./ffstream input destination format video\n"
           "i.e."
           "./ffstream rtmp://ev1.favbet.com/live/stream26 rtmp://127.0.0.1/live/mystream flv 5 \n"
         );
}

void* worker_thread(void *Param)
{
    int id = *((int*)Param);
    int idx = video_idx[id];
    int idxa = audio_idx[id];
    int ret;

    fifo_t * apackets_queue ;
    fifo_t * vpackets_queue ;
    apackets_queue = fifo_new ();
    vpackets_queue = fifo_new ();

    long vstart_time = 0;
    long astart_time = 0;
    long vdelta_time = 0;
    int vtime_trigger = 0;
    long adelta_time = 0;
    int atime_trigger = 0;
    float a2v_coeff = 1.0;
    int apackets_cnt = 0;
    int vpackets_cnt = 0;
    int a2v_measure_done = 0;

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

            if ( 0 == apackets_nb)
            {
                astart_time = get_time_ms ();
                //printf("Current time: since the Epoch %ld ms \n",  vstart_time );
            }
            while (adelay > fifo_len (apackets_queue))
            {
                adelta_time = get_time_ms () - astart_time;
                fifo_add (apackets_queue, (void*) &pkt[id] );
                //printf ("fifo_len (apackets_queue)%d\n", fifo_len (apackets_queue));
                if  (!atime_trigger && adelta_time  >=  TIME100MS * quants )
                {
                    atime_trigger++;
                    apackets_cnt = apackets_nb / quants;
                    adelay = (apackets_cnt *  adelay /   MULTIPLIER - apackets_cnt) ;
                    if ( adelay < apackets_nb ) {
                        adelay = apackets_nb;
                    }
                    if  (MAX_BUFF_SIZE <= adelay) {
                        adelay = MAX_BUFF_SIZE;
                        fprintf(stderr, "Audio delay buffer going to be big %d \n",adelay);
                    }
                    printf("delta time: %ld ms , audio packets count = %d \n", adelta_time,  apackets_nb);
                }
                apackets_nb++;
                if (vtime_trigger && atime_trigger && vpackets_nb && a2v_coeff == 1.0 ) {
                    a2v_coeff = (float) vpackets_cnt/(float) apackets_cnt;
                    printf("a2v_coeff: %f ms , apackets = %d vpackets = %d\n", a2v_coeff, apackets_cnt, vpackets_cnt);
                    printf("adelay: %d , vdelay= %d \n" , adelay, vdelay);
                }

            }
            fifo_add (apackets_queue,  (void*)&pkt[id]);
            pkt[id] = *(AVPacket *)get_head (apackets_queue);
            fifo_remove (apackets_queue);
            ret = av_interleaved_write_frame(ofmt_ctx[id], &pkt[id]);
            //apackets_nb++;

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
        {
            av_free_packet(&pkt[id]);
            continue;
        }

        AVStream *in_stream = ifmt_ctx[id]->streams[pkt[id].stream_index];
        AVStream *out_stream = ofmt_ctx[id]->streams[pkt[id].stream_index];
        AVRational time_base = ifmt_ctx[id]->streams[idx]->time_base;


#ifdef LIVE_STREAM    
        int time = 1000;
#else
        int time = 1000 * 1000 * strtof(av_ts2timestr(pkt[id].duration, &time_base), NULL);
#endif        
        usleep(time);

        if ( 0 == vpackets_nb){
            vstart_time = get_time_ms ();
            //printf("Current time: since the Epoch %ld ms \n",  vstart_time );
        }

        while (vdelay > fifo_len (vpackets_queue))
        {
            vdelta_time = get_time_ms () - vstart_time;

            fifo_add (vpackets_queue, (void*) &pkt[id] );
            //printf ("fifo_len (vpackets_queue)%d\n", fifo_len (vpackets_queue));
            if  (!vtime_trigger && vdelta_time  >=  TIME100MS * quants )
            {
                vtime_trigger++;
                vpackets_cnt = vpackets_nb / quants;
                vdelay = (vpackets_cnt *  vdelay / MULTIPLIER - vpackets_cnt) ;
                if ( vdelay < vpackets_nb )
                    vdelay = vpackets_nb;
                if (MAX_BUFF_SIZE <= vdelay){
                    vdelay = MAX_BUFF_SIZE;
                    fprintf(stderr, "Video delay buffer going to be big %d \n",vdelay);
                }
                printf("delta time: %ld ms , video packets count = %d \n", vdelta_time,  vpackets_nb);
            }
            vpackets_nb++;
            if (vtime_trigger && atime_trigger && apackets_nb && a2v_coeff == 1.0 ) {
                a2v_coeff = (float) vpackets_cnt/(float) apackets_cnt;
                printf("a2v_coeff: %f ms , apackets = %d vpackets = %d\n", a2v_coeff, apackets_cnt, vpackets_cnt);
                printf("adelay: %d , vdelay= %d \n" , adelay, vdelay);
            }
        }

        fifo_add (vpackets_queue,  (void*)&pkt[id]);
        pkt[id] = *(AVPacket *)get_head (vpackets_queue);
        fifo_remove (vpackets_queue);

        //vpackets_nb++;

        ret = av_interleaved_write_frame(ofmt_ctx[id], &pkt[id]);

        ret = 0;
        if (ret < 0)
       {
           fprintf(stderr, "Error muxing packet thread %d\n", id);
           break;
       }

       av_free_packet(&pkt[id]);

    }  // while
}

int main(int argc, char **argv)
{
    char *in_filename, *destination, *out_format ;
    char out_filename[64];
    int ret, i = 0;

    AVCodecContext  *pVCodecCtx, *pACodecCtx;
    AVCodec         *pVCodec,  *pACodec;

    static int video_stream_idx = -1, audio_stream_idx = -1;

    memset(ofmt, 0, sizeof(ofmt));
    memset(ofmt_ctx, 0, sizeof(ofmt_ctx));

    memset(ifmt_ctx, 0, sizeof(ifmt_ctx));
    memset(video_idx, -1, sizeof(video_idx));

    if  (argc < 5)
    {
        print_usage();
        exit (0);    
    }

    in_filename  = argv[1];
    destination = argv[2];
    out_format = argv[3];
    vdelay = MULTIPLIER * atoi (argv[4]);
    //quants = 1;
    if (vdelay > MULTIPLIER * 5)
        quants = 2;
    if (vdelay > MULTIPLIER * 10)
        quants = 3;
    if (vdelay > MULTIPLIER * 15)
        quants = 4;

    adelay = vdelay;
    if  (0 == vdelay || 3 >= vdelay  )
    {
        adelay = A_DELAY ;
        vdelay = V_DELAY ;
    }

    printf ("video delay = %d , audio delay = %d\n", vdelay ,adelay);
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

        ofmt[i] = ofmt_ctx[i]->oformat;

        int k;
        int res;
        for (k = 0; k < ifmt_ctx[i]->nb_streams; k++)
        {
            if (ifmt_ctx[i]->streams[k]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
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

                ret = avcodec_copy_context(out_stream->codec, ina_stream->codec);
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

                out_stream->codec->codec_tag = 0;
                if (ofmt_ctx[i]->oformat->flags & AVFMT_GLOBALHEADER)
                    { out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;}

                audio_idx[i] = k;
                fprintf(stdout, "input audio @stream #%d restreamed to\n",audio_idx[i+1]);
            }
        } // for

         if (!(ofmt[i]->flags & AVFMT_NOFILE))
        {
            ret = avio_open(&ofmt_ctx[i]->pb, out_filename, AVIO_FLAG_WRITE);

            if (ret < 0)
            {
                fprintf(stderr, "Could not open output file '%s'", out_filename);
                goto end;
            }
        }

        fprintf(stdout, "Dump output format\n");
        ret = avformat_write_header(ofmt_ctx[i], NULL);
        av_dump_format(ofmt_ctx[i], 0, out_filename, 1);

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
        av_write_trailer(ofmt_ctx[i]);
        avformat_close_input(&ifmt_ctx[i]);

        if (ofmt_ctx[i] && !(ofmt[i]->flags & AVFMT_NOFILE))
            avio_closep(&ofmt_ctx[i]->pb);
        avformat_free_context(ofmt_ctx[i]);

        if (ret < 0 && ret != AVERROR_EOF)
        {
            fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
            return 1;
        }
    }
    return 0;
}
