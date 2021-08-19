//
// Created by Administrator on 2021/8/19.
//

#include "stdio.h"
#include "stdint.h"
#include "stdlib.h"

#include "libavcodec/avcodec.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/samplefmt.h"

static int check_sample_format(const AVCodec *codec, enum AVSampleFormat sampleFormat){
    const enum  AVSampleFormat *p = codec->sample_fmts;
    while (*p != AV_SAMPLE_FMT_NONE){
        if(*p == sampleFormat){
            return 1;
        }
        p++;
    }
    return 0;
}

static int select_sample_rate(const AVCodec *codec){
    const int *p;
    int best_samplerate = 0;
    if(!codec->supported_samplerates){
        return 44100;
    }
    p = codec->supported_samplerates;
    while(*p){
        if(!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate)){
            best_samplerate = *p;
        }
        p++;
    }
    return best_samplerate;
}

static uint64_t select_channel_layout(const AVCodec *codec) {
    const uint64_t *p;
    uint64_t best_ch_layout = 0;
    int best_nb_channels = 0;

    if(!codec->channel_layouts){
        return AV_CH_LAYOUT_STEREO;
    }

    p = codec->channel_layouts;
    while(*p){
        int nb_channels = av_get_channel_layout_nb_channels(*p);
        if(nb_channels > best_nb_channels){
            best_ch_layout = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}

static void encode(AVCodecContext *context, AVFrame *frame, AVPacket *packet, FILE *output){
    int ret;
    //send the frame for encoding
    ret = avcodec_send_frame(context, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        exit(1);
    }

    //read all the available output packets
    while(ret >= 0){
        ret = avcodec_receive_packet(context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            exit(1);
        }

        fwrite(packet->data, 1, packet->size, output);
        av_packet_unref(packet);
    }
}

int main(int argc, char **argv){
    const char *filename;
    const AVCodec *codec;
    AVCodecContext *context = NULL;
    AVFrame *frame;
    AVPacket *packet;
    int i, j, k, ret;
    FILE *file;
    uint16_t *samples;
    float t, tincr;

    if (argc <= 1) {
        fprintf(stderr, "Usage: %s <output file>\n", argv[0]);
        return 0;
    }
    filename = argv[1];

    codec = avcodec_find_encoder(AV_CODEC_ID_MP2);
    if(!codec){
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    context = avcodec_alloc_context3(codec);
    if(!context){
        fprintf(stderr, "Could not allocate audio codec context\n");
        exit(1);
    }

    context->bit_rate = 64000;
    context->sample_fmt = AV_SAMPLE_FMT_S16;
    if(!check_sample_format(codec, context->sample_fmt)){
        fprintf(stderr, "Encoder does not support sample format %s",
                av_get_sample_fmt_name(context->sample_fmt));
        exit(1);
    }

    context->sample_rate = select_sample_rate(codec);
    context->channel_layout = select_channel_layout(codec);
    context->channels = av_get_channel_layout_nb_channels(context->channel_layout);

    if(avcodec_open2(context, codec, NULL) < 0){
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    /* packet for holding encoded output */
    packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "could not allocate the packet\n");
        exit(1);
    }

    /* frame containing input raw audio */
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate audio frame\n");
        exit(1);
    }

    frame->nb_samples = context->frame_size;
    frame->format = context->sample_fmt;
    frame->channel_layout = context->channel_layout;

    //allocate the data buffers
    ret = av_frame_get_buffer(frame, 0);
    if(ret < 0){
        fprintf(stderr, "Could not allocate audio data buffers\n");
        exit(1);
    }

    t = 0;
    tincr = 2 * M_PI * 440.0 / context->sample_rate;
    for(i = 0; i < 200; i++){
        //确保frame可写入，
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);
        samples = (uint16_t *)frame->data[0];
        for(j=0; j<context->frame_size;j++){
            samples[2*j] = (int) (sin(t) * 10000);
            for(k =1;k<context->channels; k++){
                samples[2*j + k] = samples[2 * j];
            }
            t += tincr;
        }
        encode(context, frame, packet, file);
    }
    encode(context, NULL, packet, file);

    fclose(file);

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
    return 0;
}