//
// Created by Administrator on 2021/8/20.
//

#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "unistd.h"

int main(int argc, char **argv){
    AVDictionary *options = NULL;
    AVIOContext *client = NULL, *server = NULL;
    const char *in_uri, *out_uri;
    int ret, pid;
    av_log_set_level(AV_LOG_TRACE);

    if (argc < 3) {
        printf("usage: %s input http://hostname[:port]\n"
               "API example program to serve http to multiple clients.\n"
               "\n", argv[0]);
        return 1;
    }

    in_uri = argv[1];
    out_uri = argv[2];

    avformat_network_init();

    if((ret = av_dict_set(&options, "listen", "2", 0)) < 0){
        fprintf(stderr, "Failed to set listen mode for server: %s\n", av_err2str(ret));
        return ret;
    }

    if((ret = avio_open2(&server, out_uri, AVIO_FLAG_WRITE, NULL, &options)) < 0){
        fprintf(stderr, "Failed to open server: %s\n", av_err2str(ret));
        return ret;
    }
    
    fprintf(stderr, "Entering main loop.\n");
    
    for(;;){
        if((ret = avio_accept(server, &client)) < 0){
            goto end;
        }
        fprintf(stderr, "Accepted client, forking process.\n");
//        pid = fork();
    }

end:
    avio_close(server);
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Some errors occurred: %s\n", av_err2str(ret));
        return 1;
    }
}
