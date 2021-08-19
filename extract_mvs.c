//
// Created by Administrator on 2021/8/19.
//
#include "libavutil/motion_vector.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

static AVFormatContext *input = NULL;
static AVCodecContext *videoDecodeContext = NULL;
static AVStream *videoStream = NULL;
static const char *srcFileName = NULL;

static int videoStreamIndex = -1;
static AVFrame *frame = NULL;
static int videoFrameCount = 0;

static int open_codec_context(AVFormatContext *formatContext, enum AVMediaType type){
    int ret;
    AVStream *stream;
    AVCodecContext *decodeContext = NULL;
    const AVCodec *codec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(input, type, -1, -1, &codec, 0);
    if(ret < 0){
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), srcFileName);
        return ret;
    } else{
        int streamIndex = ret;
        stream = input->streams[streamIndex];
        decodeContext = avcodec_alloc_context3(codec);
        if(!codec){
            fprintf(stderr, "Failed to allocate codec\n");
            return AVERROR(EINVAL);
        }

        ret = avcodec_parameters_to_context(decodeContext, stream->codecpar);
        if(ret < 0){
            fprintf(stderr, "Failed to copy codec parameters to codec context\n");
            return ret;
        }

        //init the video decoder
        av_dict_set(&opts, "flags2", "+export_mvs", 0);
        ret = avcodec_open2(decodeContext, codec, &opts);
        av_dict_free(&opts);
        if(ret < 0){
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }

        videoStreamIndex = streamIndex;
        videoStream = input->streams[videoStreamIndex];
        videoDecodeContext = decodeContext;
    }
    return 0;
}

int decode_packet(AVPacket *pPacket) {
    int ret = avcodec_send_packet(videoDecodeContext, pPacket);
    if(ret < 0){
        fprintf(stderr, "Error while sending a packet to the decoder: %s\n", av_err2str(ret));
        return ret;
    }

    while (ret >= 0){
        ret = avcodec_receive_frame(videoDecodeContext, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            fprintf(stderr, "Error while receiving a frame from the decoder: %s\n", av_err2str(ret));
            return ret;
        }

        if(ret >= 0){
            int i;
            AVFrameSideData *sideData;
            videoFrameCount++;
            sideData = av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS);
            if(sideData){
                const AVMotionVector *mvs = (const AVMotionVector*)sideData->data;
                for(i=0;i<sideData->size/ sizeof(*mvs); i++){
                    const AVMotionVector *mv = &mvs[i];
                    printf("%d,%2d,%2d,%2d,%4d,%4d,%4d,%4d,0x%"PRIx64"\n",
                           videoFrameCount, mv->source,
                           mv->w, mv->h, mv->src_x, mv->src_y,
                           mv->dst_x, mv->dst_y, mv->flags);
                }
            }
            av_frame_unref(frame);
        }
    }

    return 0;
}

int main(int argc, char **argv){
    int ret = 0;
    AVPacket packet = {0};

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <video>\n", argv[0]);
        exit(1);
    }
    srcFileName = argv[1];

    if(avformat_open_input(&input, srcFileName, NULL, NULL) < 0){
        fprintf(stderr, "Could not open source file %s\n", srcFileName);
        exit(1);
    }

    if(avformat_find_stream_info(input, NULL) < 0){
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    open_codec_context(input, AVMEDIA_TYPE_VIDEO);

    av_dump_format(input, 0, srcFileName, 0);

    if(!videoStream){
        fprintf(stderr, "Could not find video stream in the input, aborting\n");
        ret = 1;
        goto end;
    }

    frame = av_frame_alloc();
    if(!frame){
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    printf("framenum,source,blockw,blockh,srcx,srcy,dstx,dsty,flags\n");
    while (av_read_frame(input, &packet) >= 0){
        if(packet.stream_index == videoStreamIndex){
            ret = decode_packet(&packet);
        }
        av_packet_unref(&packet);
        if(ret < 0){
            break;
        }
    }
    decode_packet(NULL);


end:
    avcodec_free_context(&videoDecodeContext);
    avformat_close_input(&input);
    av_frame_free(&frame);
    return 0;
}