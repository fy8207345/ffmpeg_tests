#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libpostproc/postprocess.h>
#include <libavutil/imgutils.h>

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame){
    FILE *pFile;
    char fileName[64];
    int y;
    sprintf(fileName, "frame%d.ppm", iFrame);
    pFile = av_fopen_utf8(fileName, "wb");
    if(pFile == NULL){
        return;
    }

    //write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    //write pixel data
    for(y=0;y<height;y++){
        fwrite(pFrame->data[0] + y*pFrame->linesize[0], 1, width * 3, pFile);
    }

    //close file
    fclose(pFile);
}

int main(int argc, const char *argv[]) {
    avdevice_register_all();
    for(int i=0;i<argc;i++){
        printf("argument %d: %s\n", i, argv[i]);
    }
    if(argc < 2){
        printf("provide file name");
        goto end;
    }

    //一定要赋初始值NULL
    AVFormatContext *input = NULL;
    if(avformat_open_input(&input, argv[1], NULL, NULL) < 0){
        printf("failed to open input file : %s", argv[1]);
        goto end;
    }

    //find stream infos from file
    if(avformat_find_stream_info(input, NULL) < 0){
        printf("failed to find stream info from file");
        goto end;
    }

    printf("duration: %lld, starttime: %lld, bitrate: %lldkb/s\n", input->duration, input->start_time, input->bit_rate / 1024);

    //print stream info
    int videoIndex = -1;
    int audioIndex = -1;
    const char* string = "";
//    av_dump_format(input, 0, string, 0);
    for(int i=0;i<input->nb_streams;i++){
        if(input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            videoIndex = i;
//            av_dump_format(input, i, argv[1], 0);
        }else if(input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            audioIndex = i;
        }
    }
    printf("string : %s\n", string);

    if(videoIndex == -1){
        goto end;
    }
    const AVCodec *videoCodec = avcodec_find_decoder(input->streams[videoIndex]->codecpar->codec_id);
    if(videoCodec == NULL){
        printf("cant find video codec %d", input->streams[videoIndex]->codecpar->codec_id);
        goto end;
    }

    printf("video codec: %s\n%s\n", videoCodec->long_name, videoCodec->name);

    AVCodecContext *videoContext = avcodec_alloc_context3(videoCodec);
    if(videoContext == NULL){
        printf("failed to allocate video context");
        goto end;
    }
    avcodec_parameters_to_context(videoContext, input->streams[videoIndex]->codecpar);
    printf("video bit rate: %lld, %dx%d\n", videoContext->bit_rate, videoContext->width, videoContext->height);

    if(avcodec_open2(videoContext, videoCodec, NULL) < 0){
        printf(" failed to open video codec context!");
        goto end;
    }

    AVFrame *videoFrame = av_frame_alloc();
    AVFrame *frameRGB = av_frame_alloc();
    if(videoFrame == NULL || frameRGB == NULL){
        printf("cant allocate frame");
        goto end;
    }

    uint8_t *buffer = NULL;
    //align为内存对其格式
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, videoContext->width, videoContext->height, 4);
    if(numBytes < 0){
        printf("av_image_get_buffer_size failed");
        goto end;
    }
    //
    if(av_image_alloc(frameRGB->data, frameRGB->linesize, videoContext->width, videoContext->height, AV_PIX_FMT_RGB24, 4) < 0){
        printf("av_image_alloc failed");
        goto end;
    }
    if(av_image_fill_arrays(frameRGB->data, frameRGB->linesize, buffer, AV_PIX_FMT_RGB24, videoContext->width, videoContext->height, 4) < 0){
        printf("av_image_fill_arrays failed");
        goto end;
    }
    struct SwsContext *swsContext = NULL;
    AVPacket *packet = av_packet_alloc();
    swsContext = sws_getContext(videoContext->width,
                                videoContext->height,
                                videoContext->pix_fmt,
                                videoContext->width,
                                videoContext->height,
                                AV_PIX_FMT_RGB24,
                                SWS_BILINEAR, NULL, NULL, NULL);
    int i =0, ret;
    while (av_read_frame(input, packet) >= 0){
        if(packet->stream_index == videoIndex){
            ret = avcodec_send_packet(videoContext, packet);
            if(ret < 0){
                printf("avcodec_send_packet failed");
            }
            while (ret >= 0){
                ret = avcodec_receive_frame(videoContext, videoFrame);
                if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                    break;
                }else if(ret < 0){
                    goto end;
                }else{
                    //
                    printf("avcodec_receive_frame : %d", ret);
                    ret = sws_scale(swsContext, (uint8_t const * const *)videoFrame->data,
                              videoFrame->linesize, 0, videoFrame->height,
                              frameRGB->data, frameRGB->linesize);
                    if(++i <= 5){
                        SaveFrame(frameRGB, videoContext->width, videoContext->height, i);
                    }
                }
            }
        }

    }

    end:
        if(packet != NULL){
            av_packet_free(&packet);
        }
        if(frameRGB != NULL){
            av_frame_free(&frameRGB);
        }
        if(videoFrame != NULL){
            av_frame_free(&videoFrame);
        }
        if(videoContext != NULL){
            avcodec_close(videoContext);
            avcodec_free_context(&videoContext);
        }
        if(input != NULL){
            avformat_free_context(input);
        }
    return 0;
}
