#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libpostproc/postprocess.h>

int main(int argc, const char *argv[]) {
    avdevice_register_all();
    for(int i=0;i<argc;i++){
        printf("argument %d: %s\n", i, argv[i]);
    }
    if(argc < 2){
        printf("provide file name");
        return -1;
    }

    //一定要赋初始值NULL
    AVFormatContext *input = NULL;
    if(avformat_open_input(&input, argv[1], NULL, NULL) < 0){
        printf("failed to open input file : %s", argv[1]);
        return -2;
    }

    //find stream infos from file
    if(avformat_find_stream_info(input, NULL) < 0){
        printf("failed to find stream info from file");
        return -3;
    }

    printf("duration: %lld, starttime: %lld, bitrate: %lldkb/s\n", input->duration, input->start_time, input->bit_rate / 1024);

    //print stream info
    int videoIndex = -1;
    int audioIndex = -1;
    const char* string = "";
    av_dump_format(input, 0, string, 0);
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
        return -5;
    }
    const AVCodec *videoCodec = avcodec_find_decoder(input->streams[videoIndex]->codecpar->codec_id);
    if(videoCodec == NULL){
        printf("cant find video codec %d", input->streams[videoIndex]->codecpar->codec_id);
        return -6;
    }

    printf("video codec: %s\n%s\n", videoCodec->long_name, videoCodec->name);

    AVCodecContext *videoContext = avcodec_alloc_context3(videoCodec);
    if(videoContext == NULL){
        printf("failed to allocate video context");
        return -7;
    }
    avcodec_parameters_to_context(videoContext, input->streams[videoIndex]->codecpar);
    printf("video bit rate: %lld, %dx%d\n", videoContext->bit_rate, videoContext->width, videoContext->height);

    if(avcodec_open2(videoContext, videoCodec, NULL) < 0){
        printf(" failed to open video codec context!");
        return -8;
    }



    avcodec_close(videoContext);
    avcodec_free_context(&videoContext);
    avformat_free_context(input);
    return 0;
}
