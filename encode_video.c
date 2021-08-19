//
// Created by Administrator on 2021/8/19.
//
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"

static void encode(AVCodecContext *encodeContext, AVFrame *frame, AVPacket *packet, FILE *outfile){
    int ret;
    if(frame){
        printf("Send frame %3"PRId64"\n", frame->pts);
    }
    ret = avcodec_send_frame(encodeContext, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }
    while (ret >= 0){
        ret = avcodec_receive_packet(encodeContext, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }
        printf("Write packet %3"PRId64" (size=%5d)\n", packet->pts, packet->size);
        fwrite(packet->data, 1, packet->size, outfile);
        av_packet_unref(packet);
    }
}

//第二个参数mpeg1video
int main(int argc, char **argv){
    const char *filename, *codec_name;
    const AVCodec  *codec;
    AVCodecContext *context = NULL;
    int i, ret, x, y;
    FILE *file;
    AVFrame *frame;
    AVPacket *packet;
    uint8_t fileHeader[] = {0,0,1,0xb7};
    if(argc <= 2){
        fprintf(stderr, "Usage: %s <output file> <codec name>\n", argv[0]);
        exit(0);
    }
    filename = argv[1];
    codec_name = argv[2];

//    const AVCodec *pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
//    printf("h264 codec name : %s", pCodec->name);

    codec = avcodec_find_encoder_by_name(codec_name);
    if(!codec){
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        exit(1);
    }

    context = avcodec_alloc_context3(codec);
    if(!context){
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    packet = av_packet_alloc();
    if(!packet){
        exit(1);
    }

    context->bit_rate = 400,000;
    context->width = 352;
    context->height = 288;
    context->time_base = (AVRational){1, 25};
    context->framerate = (AVRational){25, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    context->gop_size = 10;
    context->max_b_frames = 1;
    context->pix_fmt = AV_PIX_FMT_YUV420P;

    if(codec->id == AV_CODEC_ID_H264){
        av_opt_set(context->priv_data, "preset", "slow", 0);
    }

    ret = avcodec_open2(context, codec, NULL);
    if(ret < 0){
        fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
        exit(1);
    }

    file = fopen(filename, "wb");
    if(!file){
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    frame->format = context->pix_fmt;
    frame->width = context->width;
    frame->height = context->height;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }
    for(i=0;i<250;i++){
        fflush(stdout);

        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);
        //Y平面
        for(y=0;y<context->height;y++){
            for(x=0;x<context->width;x++){
                frame->data[0][y * frame->linesize[0] + x] = x + y + i *3;
            }
        }
        for(y=0;y<context->height/2;y++){
            for(x=0;x<context->width/2;x++){
                frame->data[1][y*frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y*frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        frame->pts = i;
        encode(context, frame, packet, file);
    }

    //flush
    encode(context, NULL, packet, file);

    if(codec->id == AV_CODEC_ID_MPEG1VIDEO || codec->id == AV_CODEC_ID_MPEG2VIDEO){
        fwrite(fileHeader, 1, sizeof(fileHeader), file);
    }
    fclose(file);

    avcodec_free_context(&context);
    av_frame_free(&frame);
    av_packet_free(&packet);

    return 0;
}
