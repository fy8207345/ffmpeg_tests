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

static int open_output_file(const char *filename){
    AVStream *outStream;
    AVStream *inStream;
    AVCodecContext *decodeContext, *encodeContext;
    const AVCodec *encoder;
    int ret;
    unsigned int i;

    outFormatContext = NULL;
    avformat_alloc_output_context2(&outFormatContext, NULL, NULL, filename);
    if(!outFormatContext){
        av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
        return AVERROR_UNKNOWN;
    }

    for(i=0;i<inFormatContext->nb_streams;i++){
        outStream = avformat_new_stream(outFormatContext, NULL);
        if(!outStream){
            av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }
        inStream = inFormatContext->streams[i];
        decodeContext = streamContext[i].decodeContext;
        if(decodeContext->codec_type == AVMEDIA_TYPE_VIDEO || decodeContext->codec_type == AVMEDIA_TYPE_AUDIO){
            encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
            if(!encoder){
                av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
                return AVERROR_INVALIDDATA;
            }
            encodeContext = avcodec_alloc_context3(encoder);
            if(!encodeContext){
                av_log(NULL, AV_LOG_FATAL, "Failed to allocate the encoder context\n");
                return AVERROR(ENOMEM);
            }

            if(decodeContext->codec_type == AVMEDIA_TYPE_VIDEO){
                encodeContext->width = decodeContext->width;
                encodeContext->height = decodeContext->height;
                encodeContext->sample_aspect_ratio = decodeContext->sample_aspect_ratio;
                if(encoder->pix_fmts){
                    encodeContext->pix_fmt = encoder->pix_fmts[0];
                }else{
                    encodeContext->pix_fmt = decodeContext->pix_fmt;
                }
                encodeContext->time_base = decodeContext->time_base;
            }else{
                encodeContext->sample_rate = decodeContext->sample_rate;
                encodeContext->channel_layout = decodeContext->channel_layout;
                encodeContext->channels = av_get_channel_layout_nb_channels(encodeContext->channel_layout);
                encodeContext->sample_fmt = encoder->sample_fmts[0];
                encodeContext->time_base = (AVRational) {1, encodeContext->sample_rate};
            }

            if(outFormatContext->oformat->flags & AVFMT_GLOBALHEADER){
                encodeContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            ret = avcodec_open2(encodeContext, encoder, NULL);
            if(ret < 0){
                av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n", i);
                return ret;
            }

            ret = avcodec_parameters_from_context(outStream->codecpar, encodeContext);
            if(ret < 0){
                av_log(NULL, AV_LOG_ERROR, "Failed to copy encoder parameters to output stream #%u\n", i);
                return ret;
            }

            outStream->time_base = encodeContext->time_base;
            streamContext[i].encodeContext = encodeContext;
        }else if(decodeContext->codec_type == AVMEDIA_TYPE_UNKNOWN){
            av_log(NULL, AV_LOG_FATAL, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return AVERROR_INVALIDDATA;
        } else{
            /* if this stream must be remuxed */
            ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Copying parameters for stream #%u failed\n", i);
                return ret;
            }
            outStream->time_base = inStream->time_base;
        }
    }

    av_dump_format(outFormatContext, 0, filename, 1);
    if(!(outFormatContext->oformat->flags & AVFMT_NOFILE)){
        ret = avio_open(&outFormatContext->pb, filename, AVIO_FLAG_WRITE);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", filename);
            return ret;
        }
    }

    ret = avformat_write_header(outFormatContext, NULL);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }

    return 0;
}

static int open_input_file(const char * filename){
    int ret;
    unsigned int i;
    inFormatContext = NULL;
    if((ret = avformat_open_input(&inFormatContext, filename, NULL, NULL)) < 0){
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }
    if((ret = avformat_find_stream_info(inFormatContext, NULL)) < 0){
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    streamContext = av_malloc_array(inFormatContext->nb_streams, sizeof(streamContext));
    if(!streamContext){
        return AVERROR(ENOMEM);
    }

    for(i=0;i < inFormatContext->nb_streams; i++){
        AVStream *stream = inFormatContext->streams[i];
        const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *decodeContext;
        if(!decoder){
            av_log(NULL, AV_LOG_ERROR, "Failed to find decoder for stream #%u\n", i);
            return AVERROR_DECODER_NOT_FOUND;
        }
        decodeContext = avcodec_alloc_context3(decoder);
        if(!decodeContext){
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate the decoder context for stream #%u\n", i);
            return AVERROR(ENOMEM);
        }
        ret = avcodec_parameters_to_context(decodeContext, stream->codecpar);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "Failed to copy decoder parameters to input decoder context "
                                       "for stream #%u\n", i);
            return ret;
        }
        // reencode video & audio and remux subtitles etc
        if(decodeContext->codec_type == AVMEDIA_TYPE_VIDEO || decodeContext->codec_type == AVMEDIA_TYPE_AUDIO){
            if(decodeContext->codec_type == AVMEDIA_TYPE_VIDEO){
                decodeContext->framerate = av_guess_frame_rate(inFormatContext, stream, NULL);
            }
            //open decoder
            ret = avcodec_open2(decodeContext, decoder, NULL);
            if(ret < 0){
                av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                return ret;
            }
        }
        streamContext[i].decodeContext = decodeContext;
        streamContext[i].decodeFrame = av_frame_alloc();
        if(!streamContext[i].decodeFrame){
            return AVERROR(ENOMEM);
        }
    }

    av_dump_format(inFormatContext, 0, filename, 0);
    return 0;
}

static int init_filter(FilteringContext *pFilteringContext, AVCodecContext *decodeContext, AVCodecContext *encodeContext, const char *filterSpec){
    char args[512];
    int ret = 0;
    const AVFilter *src = NULL;
    const AVFilter *sink = NULL;
    AVFilterContext *srcContext = NULL;
    AVFilterContext *sinkContext = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVFilterGraph *filterGraph = avfilter_graph_alloc();

    if(!outputs || !inputs || !filterGraph){
        ret = AVERROR(ENOMEM);
        goto end;
    }
    if(decodeContext->codec_type == AVMEDIA_TYPE_VIDEO){
        src = avfilter_get_by_name("buffer");
        sink = avfilter_get_by_name("buffersink");
        if(!src || !sink){
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
                 "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                 decodeContext->width, decodeContext->height, decodeContext->pix_fmt,
                 decodeContext->time_base.num, decodeContext->time_base.den,
                 decodeContext->sample_aspect_ratio.num,
                 decodeContext->sample_aspect_ratio.den);

        ret = avfilter_graph_create_filter(&srcContext, src, "in", args, NULL, filterGraph);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&sinkContext, sink, "out", NULL, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(sinkContext, "pix_fmts", (uint8_t*)&encodeContext->pix_fmt, sizeof(encodeContext->pix_fmt), AV_OPT_SEARCH_CHILDREN);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    }else if(decodeContext->codec_type == AVMEDIA_TYPE_AUDIO){
        src = avfilter_get_by_name("abuffer");
        sink = avfilter_get_by_name("abuffersink");
        if(!src || !sink){
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if(!decodeContext->channel_layout){
            decodeContext->channel_layout = av_get_default_channel_layout(decodeContext->channels);
        }
        snprintf(args, sizeof(args),
                 "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
                 decodeContext->time_base.num, decodeContext->time_base.den, decodeContext->sample_rate,
                 av_get_sample_fmt_name(decodeContext->sample_fmt),
                 decodeContext->channel_layout);
        ret = avfilter_graph_create_filter(&srcContext, src, "in", args, NULL, filterGraph);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&sinkContext, sink, "out", NULL, NULL, filterGraph);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(sinkContext, "sample_fmts", (uint8_t*)&encodeContext->sample_fmt, sizeof(encodeContext->sample_fmt), AV_OPT_SEARCH_CHILDREN);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        ret = av_opt_set_bin(sinkContext, "channel_layouts", (uint8_t*)&encodeContext->channel_layout, sizeof(encodeContext->channel_layout), AV_OPT_SEARCH_CHILDREN);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

        ret = av_opt_set_bin(sinkContext, "sample_rates", (uint8_t*)&encodeContext->sample_rate, sizeof(encodeContext->sample_rate), AV_OPT_SEARCH_CHILDREN);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
            goto end;
        }
    } else{
        ret = AVERROR_UNKNOWN;
        goto end;
    }



end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

int main(int argc, char **argv){

    return 0;
}