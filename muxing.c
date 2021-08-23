//
// Created by Administrator on 2021/8/23.
//

#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "math.h"

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/timestamp.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"

#define STREAM_DURATION 10.0
#define STREAM_FRAME_RATE 25
#define STREAM_PIX_FMX AV_PIX_FMT_YUV420P

#define SCALE_FLAGS SWS_BICUBIC

typedef struct OutputStream{
    AVStream *stream;
    AVCodecContext *encodeContext;

    /* pts of the next frame that will be generated */
    int64_t nextPts;
    int sampleCount;

    AVFrame *frame;
    AVFrame *tmpFrame;

    float t, tincr, tincr2;

    struct SwsContext *swsContext;
    struct SwrContext *swrContext;
} OutputStream;

static void addStream(OutputStream *outputStream, AVFormatContext *outputContext, const AVCodec **codec, enum AVCodecID codecId){
    AVCodecContext *codecContext;
    int i;
    *codec = avcodec_find_encoder(codecId);
    if(!codec){
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codecId));
        exit(1);
    }

    outputStream->stream = avformat_new_stream(outputContext, NULL);
    if(!outputStream->stream){
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }

    outputStream->stream->id = outputContext->nb_streams - 1;
    codecContext = avcodec_alloc_context3(*codec);
    if(!codecContext){
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    outputStream->encodeContext = codecContext;

    switch ((*codec)->type) {
        case AVMEDIA_TYPE_AUDIO:
            codecContext->sample_fmt = (*codec)->sample_fmts ? (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
            codecContext->bit_rate = 64100;
            codecContext->sample_rate = 44100;
            if((*codec)->supported_samplerates){
                codecContext->sample_rate = (*codec)->supported_samplerates[0];
                for(i=0;(*codec)->supported_samplerates[i];i++){
                    if((*codec)->supported_samplerates[i] == 44100){
                        codecContext->sample_rate = 44100;
                    }
                }
            }
            codecContext->channels = av_get_channel_layout_nb_channels(codecContext->channel_layout);
        case AVMEDIA_TYPE_VIDEO:
            codecContext->codec_id = codecId;
            codecContext->bit_rate = 400000;
            codecContext->width = 352;
            codecContext->height = 288;
            //分数，1为分子/ STREAM_FRAME_RATE为分母
            codecContext->time_base = (AVRational){1, STREAM_FRAME_RATE};
            outputStream->stream->time_base = codecContext->time_base;
            codecContext->gop_size / 12;
            codecContext->pix_fmt = STREAM_PIX_FMX;
            if(codecContext->codec_id == AV_CODEC_ID_MPEG2VIDEO){
                /* just for testing, we also add B-frames */
                codecContext->max_b_frames = 2;
            }
            if(codecContext->codec_id == AV_CODEC_ID_MPEG1VIDEO){
                /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
                codecContext->mb_decision = 2;
            }
            break;
        default:
            break;
    }
    if(outputContext->oformat->flags & AVFMT_GLOBALHEADER){
        codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
}

int main(int argc, char **argv){
    OutputStream video = {0}, audio = {0};
    const AVOutputFormat *outputFormat;
    const char *filename;
    AVFormatContext *outputContext;
    const AVCodec *audioCodec, *videoCodec;
    int ret;
    int haveVideo = 0, haveAudio = 0;
    int encodeVideo = 0, encodeAudio = 0;
    AVDictionary *opt = NULL;
    int i;
    if(argc < 2){
        printf("usage: %s output_file\n"
               "API example program to output a media file with libavformat.\n"
               "This program generates a synthetic audio and video stream, encodes and\n"
               "muxes them into a file named output_file.\n"
               "The output format is automatically guessed according to the file extension.\n"
               "Raw images can also be output by using '%%d' in the filename.\n"
               "\n", argv[0]);
        return 1;
    }
    filename = argv[1];
    for (i = 2; i+1 < argc; i+=2) {
        if (!strcmp(argv[i], "-flags") || !strcmp(argv[i], "-fflags"))
            av_dict_set(&opt, argv[i]+1, argv[i+1], 0);
    }

    avformat_alloc_output_context2(&outputContext, NULL, NULL, filename);
    if(!outputContext){
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&outputContext, NULL, "mpeg", filename);
    }

    if(!outputContext){
        return 1;
    }
    outputFormat = outputContext->oformat;

    if(outputFormat->video_codec != AV_CODEC_ID_NONE){

    }
}