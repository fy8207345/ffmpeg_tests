//
// Created by Administrator on 2021/8/20.
//

#include "stdio.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavutil/hwcontext.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"

static AVBufferRef *hardwareDeviceContext = NULL;
static enum AVPixelFormat hardwarePixelFormat;
static FILE *outputFile = NULL;

static int hw_decoder_init(AVCodecContext *context, const enum AVHWDeviceType type){
    int err = 0;
    if((err = av_hwdevice_ctx_create(&hardwareDeviceContext, type, NULL, NULL, 0) < 0)){
        fprintf(stderr, "Failed to create specified HW device.\n");
        return err;
    }
    context->hw_device_ctx = av_buffer_ref(hardwareDeviceContext);

    return err;
}

static enum AVPixelFormat get_hw_format(AVCodecContext *context, const enum AVPixelFormat *avPixelFormat){
    const enum AVPixelFormat *p;
    for(p=avPixelFormat; *p != -1 ;p++){
        if(*p == hardwarePixelFormat){
            return *p;
        }
    }
    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

static int decode_write(AVCodecContext *context, AVPacket *packet){
    AVFrame *frame = NULL, *swFrame = NULL;
    AVFrame *tmpFrame = NULL;
    uint8_t *buffer = NULL;
    int size;
    int ret = 0;

    ret = avcodec_send_packet(context, packet);
    if(ret < 0){
        fprintf(stderr, "Error during decoding\n");
        return ret;
    }

    while (1){
        if(!(frame = av_frame_alloc()) || !(swFrame = av_frame_alloc())){
            fprintf(stderr, "Can not alloc frame\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = avcodec_receive_frame(context, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&swFrame);
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding\n");
            goto fail;
        }

        if(frame->format == hardwarePixelFormat){
            //retrive data from gpu to cpu
            if((ret = av_hwframe_transfer_data(swFrame, frame, 0)) < 0){
                fprintf(stderr, "Error transferring the data to system memory\n");
                goto fail;
            }
            tmpFrame = swFrame;
        }else{
            tmpFrame = frame;
        }

        size = av_image_get_buffer_size(tmpFrame->format, tmpFrame->width, tmpFrame->height, 1);
        buffer = av_malloc(size);

        if(!buffer){
            fprintf(stderr, "Can not alloc buffer\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = av_image_copy_to_buffer(buffer, size, (const uint8_t * const *)tmpFrame->data, tmpFrame->linesize, tmpFrame->format, tmpFrame->width, tmpFrame->height, 1);
        if(ret < 0){
            fprintf(stderr, "Can not copy image to buffer\n");
            goto fail;
        }

        if((ret = fwrite(buffer, 1, size, outputFile)) < 0){
            fprintf(stderr, "Failed to dump raw data.\n");
            goto fail;
        }

    fail:
        av_frame_free(&frame);
        av_frame_free(&swFrame);
        av_freep(&buffer);
        if (ret < 0)
            return ret;
    }
}

int main(int argc, char *argv[]){
    AVFormatContext *input = NULL;
    int videoStream, ret;
    AVStream *video;
    AVCodecContext *decodeContext = NULL;
    const AVCodec *decoder = NULL;
    enum  AVHWDeviceType type;
    AVPacket packet;
    int i;

    if (argc < 4) {
        fprintf(stderr, "Usage: %s <device type> <input file> <output file>\n", argv[0]);
        return -1;
    }

    type = av_hwdevice_find_type_by_name(argv[1]);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", argv[1]);
        fprintf(stderr, "Available device types:");
        while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return -1;
    }

    /* open the input file */
    if (avformat_open_input(&input, argv[2], NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", argv[2]);
        return -1;
    }

    if (avformat_find_stream_info(input, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }

    /* find the video stream information */
    ret = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    videoStream = ret;

    for(i=0;;i++){
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if(!config){
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    decoder->name, av_hwdevice_get_type_name(type));
            return -1;
        }
        if(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type){
            hardwarePixelFormat = config->pix_fmt;
            break;
        }
    }

    if(!(decodeContext = avcodec_alloc_context3(decoder))){
        return AVERROR(ENOMEM);
    }

    video = input->streams[videoStream];
    if(avcodec_parameters_to_context(decodeContext, video->codecpar) < 0){
        return -1;
    }
    decodeContext->get_format = get_hw_format;

    if(hw_decoder_init(decodeContext, type) < 0){
        return -1;
    }

    if((ret = avcodec_open2(decodeContext, decoder, NULL)) < 0){
        fprintf(stderr, "Failed to open codec for stream #%u\n", videoStream);
        return -1;
    }

    /* open the file to dump raw data */
    outputFile = fopen(argv[3], "w+b");

    while(ret >= 0){
        if((ret = av_read_frame(input, &packet)) < 0){
            break;
        }
        if(videoStream == packet.stream_index){
            ret = decode_write(decodeContext, &packet);
        }
        av_packet_unref(&packet);
    }

    packet.data = NULL;
    packet.size = 0;
    decode_write(decodeContext, &packet);
    av_packet_unref(&packet);

    if (outputFile)
        fclose(outputFile);
    avcodec_free_context(&decodeContext);
    avformat_close_input(&input);
    av_buffer_unref(&hardwareDeviceContext);

    avformat_network_deinit();

    return 0;
}