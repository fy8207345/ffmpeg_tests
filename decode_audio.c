//
// Created by Administrator on 2021/8/19.
//
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavcodec/avcodec.h"

#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

static void decode(AVCodecContext *decodeContext, AVPacket *packet, AVFrame *frame, FILE *outfile){
    int i, ret, ch, data_size;
    ret = avcodec_send_packet(decodeContext, packet);
    if(ret < 0){
        fprintf(stderr, "Error submitting the packet to the decoder\n");
        exit(1);
    }
    while (ret >= 0){
        ret = avcodec_receive_frame(decodeContext, frame);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            return;
        }else if(ret < 0){
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }
        data_size = av_get_bytes_per_sample(decodeContext->sample_fmt);
        if(data_size < 0){
            /* This should not occur, checking just for paranoia */
            fprintf(stderr, "Failed to calculate data size\n");
            exit(1);
        }
        for(i=0; i < frame->nb_samples; i++){
            for(ch = 0; ch < decodeContext->channels; ch++){
                fwrite(frame->data[ch] + data_size * i, 1, data_size, outfile);
            }
        }
    }
}

static int get_format_from_sample_format(const char **format, enum AVSampleFormat sampleFormat){
    int i;
    struct sample_format_entry {
        enum AVSampleFormat avSampleFormat;
        const char *fmt_be, *fmt_le;
    } sampleFormatEntries[] = {
            { AV_SAMPLE_FMT_U8,  "u8",    "u8"    },
            { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
            { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
            { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
            { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
    };
    *format = NULL;

    for(i=0;i < FF_ARRAY_ELEMS(sampleFormatEntries); i++){
        struct sample_format_entry *entry = &sampleFormatEntries[i];
        if(sampleFormat == entry->avSampleFormat){
            *format = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    fprintf(stderr,
            "sample format %s is not supported as output format\n",
            av_get_sample_fmt_name(sampleFormat));
    return -1;
}

int main(int argc, char **argv) {
    const char *outfilename, *filename;
    const AVCodec  *codec;
    AVCodecContext *context = NULL;
    AVCodecParserContext *parserContext = NULL;
    int len, ret;
    FILE *f, *outfile;
    uint8_t inbuf[AUDIO_INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t *data;
    size_t data_size;
    AVPacket *packet;
    AVFrame *decoded_frame = NULL;
    enum AVSampleFormat sampleFormat;
    int n_channels = 0;
    const char *format;

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
    }
    filename    = argv[1];
    outfilename = argv[2];

    packet = av_packet_alloc();
    codec = avcodec_find_decoder(AV_CODEC_ID_MP2);
    if(!codec){
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    parserContext = av_parser_init(codec->id);
    if(!parserContext){
        fprintf(stderr, "Parser not found\n");
        exit(1);
    }

    context = avcodec_alloc_context3(codec);
    if(!context){
        fprintf(stderr, "Could not allocate audio codec context\n");
        exit(1);
    }

    if(avcodec_open2(context, codec, NULL) < 0){
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    outfile = fopen(outfilename, "wb");
    if (!outfile) {
        av_free(context);
        exit(1);
    }

    data = inbuf;
    data_size = fread(inbuf, 1, AUDIO_INBUF_SIZE, f);

    while (data_size > 0){
        if(!decoded_frame){
            if(!(decoded_frame = av_frame_alloc())){
                fprintf(stderr, "Could not allocate audio frame\n");
                exit(1);
            }
        }

        ret = av_parser_parse2(parserContext, context, &packet->data, &packet->size,
                               data, (int)data_size,
                               AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if(ret < 0){
            fprintf(stderr, "Error while parsing\n");
            exit(1);
        }

        data += ret;
        data_size -= ret;

        if(packet->size){
            decode(context, packet, decoded_frame, outfile);
        }

        if(data_size < AUDIO_REFILL_THRESH){
            memmove(inbuf, data, data_size);
            data = inbuf;
            len = fread(data + data_size, 1, AUDIO_INBUF_SIZE - data_size, f);
            if(len > 0){
                data_size += len;
            }
        }
    }

    packet->data = NULL;
    packet->size = 0;
    decode(context, packet, decoded_frame, outfile);

    sampleFormat = context->sample_fmt;

    if(av_sample_fmt_is_planar(sampleFormat)){
        const char *packed = av_get_sample_fmt_name(sampleFormat);
        printf("Warning: the sample format the decoder produced is planar "
               "(%s). This example will output the first channel only.\n",
               packed ? packed : "?");
        sampleFormat = av_get_packed_sample_fmt(sampleFormat);
    }

    n_channels = context->channels;
    if((ret = get_format_from_sample_format(&format, sampleFormat)) < 0){
        goto end;
    }
    printf("Play the output audio file with the command:\n"
           "ffplay -f %s -ac %d -ar %d %s\n",
           format, n_channels, context->sample_rate,
           outfilename);

end:
    fclose(outfile);
    fclose(f);

    avcodec_free_context(&context);
    av_parser_close(parserContext);
    av_frame_free(&decoded_frame);
    av_packet_free(&packet);
    return 0;
}