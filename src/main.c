#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <unistd.h>
#include <sys/stat.h>

int file_exists(char *filename)
{
    struct stat buffer;

    return stat(filename, &buffer) == 0;
}

void process_client(AVIOContext *client)
{
    AVIOContext *input = NULL;
    uint8_t buf[1024];
    int ret, n, reply_code;
    char *resource = NULL;
    char filepath[50] = "assets/";

    while ((ret = avio_handshake(client)) > 0) {
        av_opt_get(client, "resource", AV_OPT_SEARCH_CHILDREN, (unsigned char **)&resource);
        // check for strlen(resource) is necessary, because av_opt_get()
        // may return empty string.
        if (resource && strlen(resource))
            break;
    }
    if (ret < 0)
        goto end;
    av_log(client, AV_LOG_TRACE, "resource=%p\n", resource);
    if (resource && resource[0] == '/' && file_exists(strcat(filepath, (resource + 1)))) {
        reply_code = 200;
    } else {
        reply_code = AVERROR_HTTP_NOT_FOUND;
    }
    if ((ret = av_opt_set_int(client, "reply_code", reply_code, AV_OPT_SEARCH_CHILDREN)) < 0) {
        av_log(client, AV_LOG_ERROR, "Failed to set reply_code: %s.\n", av_err2str(ret));
        goto end;
    }
    av_log(client, AV_LOG_TRACE, "Set reply code to %d\n", reply_code);
    while ((ret = avio_handshake(client)) > 0);
    if (ret < 0)
        goto end;
    fprintf(stderr, "Handshake performed.\n");
    if (reply_code != 200)
        goto end;
    fprintf(stderr, "Opening input file.\n");
    if ((ret = avio_open2(&input, filepath, AVIO_FLAG_READ, NULL, NULL)) < 0) {
        av_log(input, AV_LOG_ERROR, "Failed to open input: %s: %s.\n", filepath,
                av_err2str(ret));
        goto end;
    }
    for(;;) {
        n = avio_read(input, buf, sizeof(buf));
        if (n < 0) {
            if (n == AVERROR_EOF)
                break;
            av_log(input, AV_LOG_ERROR, "Error reading from input: %s.\n",
                    av_err2str(n));
            break;
        }
        avio_write(client, buf, n);
        avio_flush(client);
    }
end:
    fprintf(stderr, "Flushing client\n");
    avio_flush(client);
    fprintf(stderr, "Closing client\n");
    avio_close(client);
    fprintf(stderr, "Closing input\n");
    avio_close(input);
}
int main(int argc, char **argv)
{
    av_log_set_level(AV_LOG_TRACE);
    AVDictionary *options = NULL;
    AVIOContext *client = NULL, *server = NULL;
    const char *out_uri;
    int ret, pid;
    if (argc < 2) {
        printf("usage: %s http://hostname[:port]\n"
                "API program to serve videos over http to multiple clients.\n"
                "\n", argv[0]);
        return 1;
    }
    out_uri = argv[1];
    avformat_network_init();
    if ((ret = av_dict_set(&options, "listen", "2", 0)) < 0) {
        fprintf(stderr, "Failed to set listen mode for server: %s\n", av_err2str(ret));
        return ret;
    }
    if ((ret = avio_open2(&server, out_uri, AVIO_FLAG_WRITE, NULL, &options)) < 0) {
        fprintf(stderr, "Failed to open server: %s\n", av_err2str(ret));
        return ret;
    }
    fprintf(stderr, "Entering main loop.\n");
    for(;;) {
        if ((ret = avio_accept(server, &client)) < 0)
            goto end;
        fprintf(stderr, "Accepted client, forking process.\n");
        pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            ret = AVERROR(errno);
            goto end;
        }
        if (pid == 0) {
            fprintf(stderr, "In child.\n");
            process_client(client);
            avio_close(server);
            exit(0);
        }
        if (pid > 0)
        {
            signal(SIGCHLD,SIG_IGN);
            avio_close(client);
        }
    }
end:
    avio_close(server);
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Some errors occurred: %s\n", av_err2str(ret));
        return 1;
    }
    return 0;
}
