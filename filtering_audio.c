//
// Created by Administrator on 2021/8/19.
//

#include <unistd.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"

static const char *filter_descr = "aresample=8000,aformat=sample_fmts=s16:channel_layouts=mono";
static const char *player = "ffplay -f s16le -ar 8000 -ac 1 -";

static AVFormatContext *input;
static AVCodecContext *decodeContext;
AVFilterContext *sinkContext;
AVFilterContext *srcContext;
AVFilterGraph *filterGraph;
static int audioStreamIndex = -1;

static int open_input_file(const char *filename){
    const AVCodec *codec;
    int ret;

    if((ret = avformat_open_input(&input, filename, NULL, NULL)) < 0){
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(input, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    /* select the audio stream */
    ret = av_find_best_stream(input, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find an audio stream in the input file\n");
        return ret;
    }
    audioStreamIndex = ret;

    decodeContext = avcodec_alloc_context3(codec);
    if (!decodeContext)
        return AVERROR(ENOMEM);
    avcodec_parameters_to_context(decodeContext, input->streams[audioStreamIndex]->codecpar);

    /* init the audio decoder */
    if ((ret = avcodec_open2(decodeContext, codec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open audio decoder\n");
        return ret;
    }

    return 0;
}

static int init_filters(const char *filter_description){
    char args[512];
    int ret = 0;
    const AVFilter *src = avfilter_get_by_name("abuffer");
    const AVFilter *sink = avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    static const enum AVSampleFormat outSampleFormats[] = {AV_SAMPLE_FMT_S16, -1};
    static const int64_t outChannelLayouts[] = {AV_CH_LAYOUT_MONO, -1};
    static const int outSampleRates[] = {8000, -1};
    const AVFilterLink *outlink;
    AVRational time_base = input->streams[audioStreamIndex]->time_base;

    filterGraph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filterGraph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

int main(int argc, char **argv){
    int ret;
    AVPacket packet;
    AVFrame *frame = av_frame_alloc();
    AVFrame *filt_frame = av_frame_alloc();

    if (!frame || !filt_frame) {
        perror("Could not allocate frame");
        exit(1);
    }

    if(argc != 2){
        fprintf(stderr, "Usage: %s file | %s\n", argv[0], player);
        exit(1);
    }

    if((ret = open_input_file(argv[1])) < 0){
        goto end;
    }
    if((ret = init_filters(filter_descr)) < 0){
        goto end;
    }

end:
    avfilter_graph_free(&filterGraph);
    avcodec_free_context(&decodeContext);
    avformat_close_input(&input);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }

    exit(0);
}