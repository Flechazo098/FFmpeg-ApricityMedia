#ifndef AM_CUSTOM_IO_H
#define AM_CUSTOM_IO_H

/* Private AVIO ownership. Format contexts borrow this context and never close
 * callback resources. The enclosing decoder closes format before releasing IO. */
typedef struct {
    AVIOContext *context;
    void *opaque;
    am_io_read_callback read;
    am_io_seek_callback seek;
    am_io_release_callback release;
} AmCustomInput;

static int am_custom_read(void *opaque, uint8_t *buffer, int length) {
    AmCustomInput *input = opaque;
    int read = input->read(input->opaque, buffer, length);
    if (read == -1) return AVERROR_EOF;
    if (read == 0) return AVERROR(EAGAIN);
    return read > length ? AVERROR(EIO) : read;
}

static int64_t am_custom_seek(void *opaque, int64_t offset, int whence) {
    AmCustomInput *input = opaque;
    return input->seek(input->opaque, offset, whence & ~AVSEEK_FORCE);
}

static int am_custom_prepare(AmCustomInput *input, AVFormatContext *format) {
    if (!input->read) return 0;
    if (!input->seek) return AVERROR(EINVAL);
    unsigned char *buffer = av_malloc(64 * 1024);
    if (!buffer) return AVERROR(ENOMEM);
    input->context = avio_alloc_context(buffer, 64 * 1024, 0, input,
                                        am_custom_read, NULL, am_custom_seek);
    if (!input->context) {
        av_free(buffer);
        return AVERROR(ENOMEM);
    }
    input->context->seekable = AVIO_SEEKABLE_NORMAL;
    format->pb = input->context;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    return 0;
}

static void am_custom_close(AmCustomInput *input) {
    if (input->context) {
        /* FFmpeg may replace the original buffer while probing. */
        av_freep(&input->context->buffer);
        avio_context_free(&input->context);
    }
    am_io_release_callback release = input->release;
    void *opaque = input->opaque;
    memset(input, 0, sizeof(*input));
    if (release) release(opaque);
}

#endif
