//
// Created by Administrator on 2021/8/24.
//

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

static AVFormatContext *inFormatContext;
static AVFormatContext *outFormatContext;
typedef struct FilteringContext{
    AVFilterContext *sinkContext;
    AVFilterContext *srcContext;
    AVFilterGraph *filterGraph;
    AVPacket *encodePacket;
    AVFrame *filteredFrame;
} FilteringContext;
static FilteringContext *filteringContext;

typedef struct StreamContext{
    AVCodecContext *decodeContext;
    AVCodecContext *encodeContext;
    AVFrame *decodeFrame;
} StreamContext;
static StreamContext *streamContext;

static int open_input_file(const char * filename){
    int ret;
    unsigned int i;
    inFormatContext = NULL;
}

int main(int argc, char **argv){

    return 0;
}