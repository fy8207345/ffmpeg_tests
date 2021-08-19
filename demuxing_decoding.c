//
// Created by Administrator on 2021/8/19.
//
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/timestamp.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

static AVFormatContext *input = NULL;
static AVCodecContext *videoDecodeContext = NULL, *audioDecodeContext;
static int width, height;
static enum AVPixelFormat pixelFormat;
static AVStream *videoStream = NULL, *audioStream = NULL;
static const char *srcFileName = NULL;
static const char *videoDestFileName = NULL;
static const char *audioDestFileName = NULL;
static FILE *videoDestFile = NULL;
static FILE *audioDestFile = NULL;

static uint8_t *video_dest_data[4] = {NULL};
static int video_dest_linesize[4];
static int video_dest_bufsize;

static int videoStreamIndex = -1, audioStreamIndex = -1;
static AVFrame *frame = NULL;
static AVPacket *packet = NULL;
static int videoFrameCount = 0;
static int audioFrameCount = 0;

static int output_video_frame(AVFrame *outFrame) {
    if (outFrame->width != width || outFrame->height != height || outFrame->format != pixelFormat) {
        /* To handle this change, one could call av_image_alloc again and
         * decode the following frames into another rawvideo file. */
        fprintf(stderr, "Error: Width, height and pixel format have to be "
                        "constant in a rawvideo file, but the width, height or "
                        "pixel format of the input video changed:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                width, height, av_get_pix_fmt_name(pixelFormat),
                outFrame->width, outFrame->height,
                av_get_pix_fmt_name(outFrame->format));
        return -1;
    }

    printf("video_frame n:%d coded_n:%d\n",
           videoFrameCount++, outFrame->coded_picture_number);

    //copy decoded frame to destination buffer;
    // this is required since rawvideo expects non aligned data

    //将帧数据复制到自定义的buffer中
    av_image_copy(video_dest_data, video_dest_linesize,
                  (const uint8_t **) outFrame->data, outFrame->linesize,
                  pixelFormat, width, height);

    //write to rawvideo file
    //将自定义的buffer中的数据写入到文件中
    fwrite(video_dest_data[0], 1, video_dest_bufsize, videoDestFile);
    return 0;
}

static int output_audio_frame(AVFrame *outFrame) {
    size_t unpadded_linesize = outFrame->nb_samples * av_get_bytes_per_sample(outFrame->format);
    printf("audio_frame n:%d nb_samples:%d pts:%s\n",
           audioFrameCount++, outFrame->nb_samples,
           av_ts2timestr(outFrame->pts, &audioDecodeContext->time_base));

    /* Write the raw audio data samples of the first plane. This works
     * fine for packed formats (e.g. AV_SAMPLE_FMT_S16). However,
     * most audio decoders output planar audio, which uses a separate
     * plane of audio samples for each channel (e.g. AV_SAMPLE_FMT_S16P).
     * In other words, this code will write only the first audio channel
     * in these cases.
     * You should use libswresample or libavfilter to convert the frame
     * to packed data. */
    fwrite(outFrame->extended_data[0], 1, unpadded_linesize, audioDestFile);

    return 0;
}

static int decode_packet(AVCodecContext *decodeContext, const AVPacket *decodingPacket) {
    int ret = 0;

    //将该数据包缓冲区的数据发送到解码器上下文Context中，让它解码
    ret = avcodec_send_packet(decodeContext, decodingPacket);
    if (ret < 0) {
        fprintf(stderr, "Error submitting a packet for decoding (%s)\n", av_err2str(ret));
        return ret;
    }

    //如果解码成功
    while (ret >= 0) {
        //从解码器上下文Context中接收帧数据，到frame缓冲区
        ret = avcodec_receive_frame(decodeContext, frame);
        if (ret < 0) {
            // those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding
            //AVERROR_EOF流结束标识；EAGAIN：再试一次，可能packet中包含的帧数据不完整
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return 0;

            fprintf(stderr, "Error during decoding (%s)\n", av_err2str(ret));
            return ret;
        }

        //write the frame data to output file
        if (decodeContext->codec->type == AVMEDIA_TYPE_VIDEO) {
            //输出视频文件
            ret = output_video_frame(frame);
        } else {
            ret = output_audio_frame(frame);
        }

        av_frame_unref(frame);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static int open_codec_context(int *streamIndex, AVCodecContext **decodeContext, AVFormatContext *formatContext,
                              enum AVMediaType type) {
    int ret, stream_index;
    AVStream *stream;
    const AVCodec *codec = NULL;
    ret = av_find_best_stream(formatContext, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), srcFileName);
        return ret;
    } else {
        stream_index = ret;
        //找到对于流
        stream = formatContext->streams[stream_index];

        //找该流类型的解码器
        codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        //创建该解码器的上下文环境
        *decodeContext = avcodec_alloc_context3(codec);
        if (!*decodeContext) {
            fprintf(stderr, "Failed to allocate the %s codec context\n",
                    av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        //将流信息从AVFormatContext复制到解码器上下文环境
        if ((ret = avcodec_parameters_to_context(*decodeContext, stream->codecpar)) < 0) {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                    av_get_media_type_string(type));
            return ret;
        }

        //打开解码器
        if ((ret = avcodec_open2(*decodeContext, codec, NULL)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *streamIndex = stream_index;
    }
    return 0;
}

static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt) {
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt;
        const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
            {AV_SAMPLE_FMT_U8,  "u8",    "u8"},
            {AV_SAMPLE_FMT_S16, "s16be", "s16le"},
            {AV_SAMPLE_FMT_S32, "s32be", "s32le"},
            {AV_SAMPLE_FMT_FLT, "f32be", "f32le"},
            {AV_SAMPLE_FMT_DBL, "f64be", "f64le"},
    };
    *fmt = NULL;

    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    fprintf(stderr,
            "sample format %s is not supported as output format\n",
            av_get_sample_fmt_name(sample_fmt));
    return -1;
}

int main(int argc, char **argv) {
    int ret = 0;
    if (argc != 4) {
        fprintf(stderr, "usage: %s  input_file video_output_file audio_output_file\n"
                        "API example program to show how to read frames from an input file.\n"
                        "This program reads frames from a file, decodes them, and writes decoded\n"
                        "video frames to a rawvideo file named video_output_file, and decoded\n"
                        "audio frames to a rawaudio file named audio_output_file.\n",
                argv[0]);
        exit(1);
    }
    //三个文件，一个输入合成的文件，两个输出文件：一个视频，一个音频
    srcFileName = argv[1];
    videoDestFileName = argv[2];
    audioDestFileName = argv[3];

    //打开输入流并绑定到 AVFormatContext 上
    if (avformat_open_input(&input, srcFileName, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", srcFileName);
        exit(1);
    }

    //读取流信息
    if (avformat_find_stream_info(input, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    //从流信息中查找视频流
    if (open_codec_context(&videoStreamIndex, &videoDecodeContext, input, AVMEDIA_TYPE_VIDEO) >= 0) {
        //从流中获得视频流
        videoStream = input->streams[videoStreamIndex];

        //打开视频的输出文件
        videoDestFile = av_fopen_utf8(videoDestFileName, "wb");
        if (!videoDestFile) {
            fprintf(stderr, "Could not open destination file %s\n", videoDestFileName);
            ret = 1;
            goto end;
        }

        //获取该视频通道的一些信息， 宽带，高度，像素点的格式
        width = videoDecodeContext->width;
        height = videoDecodeContext->height;
        pixelFormat = videoDecodeContext->pix_fmt;

        //创建视频解码缓冲区
        ret = av_image_alloc(video_dest_data, video_dest_linesize,
                             width, height, pixelFormat, 1);
        if (ret < 0) {
            fprintf(stderr, "Could not allocate raw video buffer\n");
            goto end;
        }
        //视频缓冲区的大小
        video_dest_bufsize = ret;
    }

    //查找音频流
    if (open_codec_context(&audioStreamIndex, &audioDecodeContext, input, AVMEDIA_TYPE_AUDIO) >= 0) {
        //获得音频流
        audioStream = input->streams[audioStreamIndex];
        //打开音频输出文件
        audioDestFile = av_fopen_utf8(audioDestFileName, "wb");
        if (!audioDestFile) {
            fprintf(stderr, "Could not open destination file %s\n", audioDestFileName);
            ret = 1;
            goto end;
        }
    }

    //打印输入文件的流信息
    av_dump_format(input, 0, srcFileName, 0);

    if (!videoStream && !audioStream) {
        fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
        ret = 1;
        goto end;
    }

    //分配接收帧的缓冲区
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    //分配接收数据包的缓冲区
    packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "Could not allocate packet\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (videoStream) {
        printf("Demuxing video from file '%s' into '%s'\n", srcFileName, videoDestFileName);
    }
    if (audioStream) {
        printf("Demuxing audio from file '%s' into '%s'\n", srcFileName, audioDestFileName);
    }

    //不断从AVFormatContext中读取流信息到packet缓冲区
    while (av_read_frame(input, packet) >= 0) {
        //判断当前包的流属于哪个
        if (packet->stream_index == videoStreamIndex) {
            ret = decode_packet(videoDecodeContext, packet);
        } else if (packet->stream_index == audioStreamIndex) {
            ret = decode_packet(audioDecodeContext, packet);
        }
        //清除该包数据的引用，
        av_packet_unref(packet);
        if (ret < 0) {
            break;
        }
    }

    //flush the decoders
    if (videoDecodeContext) {
        decode_packet(videoDecodeContext, NULL);
    }
    if (audioDecodeContext) {
        decode_packet(audioDecodeContext, NULL);
    }

    printf("Demuxing succeeded.\n");

    if (videoStream) {
        printf("Play the output video file with the command:\n"
               "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
               av_get_pix_fmt_name(pixelFormat), width, height,
               videoDestFileName);
    }

    if (audioStream) {
        enum AVSampleFormat sampleFormat = audioDecodeContext->sample_fmt;
        int nchannels = audioDecodeContext->channels;
        const char *fmt;
        if (av_sample_fmt_is_planar(sampleFormat)) {
            const char *packed = av_get_sample_fmt_name(sampleFormat);
            printf("Warning: the sample format the decoder produced is planar "
                   "(%s). This example will output the first channel only.\n",
                   packed ? packed : "?");
            sampleFormat = av_get_packed_sample_fmt(sampleFormat);
            nchannels = 1;
        }

        if ((ret = get_format_from_sample_fmt(&fmt, sampleFormat)) < 0){
            goto end;
        }

        printf("Play the output audio file with the command:\n"
               "ffplay -f %s -ac %d -ar %d %s\n",
               fmt, nchannels, audioDecodeContext->sample_rate,
               audioDestFileName);
    }

    end:
    avcodec_free_context(&videoDecodeContext);
    avcodec_free_context(&audioDecodeContext);
    avformat_close_input(&input);
    if (videoDestFile)
        fclose(videoDestFile);
    if (audioDestFile)
        fclose(audioDestFile);
    av_packet_free(&packet);
    av_frame_free(&frame);
    av_free(video_dest_data[0]);
    return ret;
}