//
// Created by Administrator on 2021/8/18.
//
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>

struct buffer_data{
    uint8_t *ptr;
    size_t size; //size left in the buffer
};

static int read_pack(void *opaque, uint8_t *buf, int buf_size){
    struct buffer_data *bufferData = (struct buffer_data*) opaque;
    buf_size = FFMIN(buf_size, bufferData->size);
    if(!buf_size){
        return AVERROR_EOF;
    }
    printf("ptr:%p size: %zu\n", bufferData->ptr, bufferData->size);

    memcpy(buf, bufferData->ptr, buf_size);
    bufferData->ptr += buf_size;
    bufferData->size -= buf_size;

    return buf_size;
}

int main(int argc, char **argv) {

    AVFormatContext *input = NULL;
    AVIOContext *avioContext = NULL;
    uint8_t *buffer = NULL, *avio_ctx_buffer = NULL;
    size_t buffer_size, avio_ctx_buffer_size = 4096;
    char *input_filename = NULL;
    int ret = 0;
    struct buffer_data bufferData = {0};

    if (argc != 2) {
        fprintf(stderr, "usage: %s input_file\n"
                        "API example program to show how to read from a custom buffer "
                        "accessed through AVIOContext.\n", argv[0]);
        return 1;
    }
    input_filename = argv[1];

    ret = av_file_map(input_filename, &buffer, &buffer_size, 0, NULL);
    if(ret < 0){
        goto end;
    }

    bufferData.ptr = buffer;
    bufferData.size = buffer_size;
    if(!(input = avformat_alloc_context())){
        ret = AVERROR(ENOMEM);
        goto end;
    }

    avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    avioContext = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0, &bufferData, &read_pack, NULL, NULL);
    if(!avioContext){
        ret = AVERROR(ENOMEM);
        goto end;
    }
    input->pb = avioContext;

    ret = avformat_open_input(&input, NULL, NULL, NULL);
    if(ret < 0){
        fprintf(stderr, "Could not open input\n");
        goto end;
    }

    ret = avformat_find_stream_info(input, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not find stream information\n");
        goto end;
    }

    av_dump_format(input, 0, input_filename, 0);

    end:
        avformat_close_input(&input);
        if(avioContext){
            av_freep(avioContext->buffer);
        }
        avio_context_free(&avioContext);

        av_file_unmap(buffer, buffer_size);

        if (ret < 0) {
            fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
            return 1;
        }
    return 0;
}