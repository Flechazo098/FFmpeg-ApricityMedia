#ifndef AM_ARCHIVE_IO_H
#define AM_ARCHIVE_IO_H

#include <errno.h>
#include <stdatomic.h>
#include <zlib.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#endif

/* Archive bytes never cross the Java boundary. Inflate checkpoints retain
 * zlib's dictionary and bit state, rather than replaying a whole ZIP entry.
 * Checkpoints are shared only while cursors for the same immutable file live. */
#define AM_ARCHIVE_BUFFER (64 * 1024)
#define AM_ARCHIVE_POINTS 128
static _Atomic int64_t am_archive_metrics[12];

#if defined(_WIN32)
typedef SRWLOCK AmArchiveMutex;
#define AM_ARCHIVE_MUTEX_INIT SRWLOCK_INIT
static void am_archive_lock(AmArchiveMutex *m) { AcquireSRWLockExclusive(m); }
static void am_archive_unlock(AmArchiveMutex *m) { ReleaseSRWLockExclusive(m); }
typedef struct { DWORD volume, high, low; uint64_t modified, size; } AmArchiveIdentity;
typedef struct { HANDLE handle, event; AmArchiveIdentity identity; } AmArchiveFile;
static int am_archive_file_open(AmArchiveFile *file, const char *name) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, NULL, 0);
    if (!count) return AVERROR(EINVAL);
    wchar_t *wide = malloc((size_t)count * sizeof(*wide));
    if (!wide) return AVERROR(ENOMEM);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide, count);
    file->handle = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_RANDOM_ACCESS, NULL);
    free(wide);
    if (file->handle == INVALID_HANDLE_VALUE) { file->handle = NULL; return AVERROR(ENOENT); }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(file->handle, &info)) return AVERROR(EIO);
    file->identity = (AmArchiveIdentity) { info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow,
        ((uint64_t)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime,
        ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow };
    file->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    return file->event ? 0 : AVERROR(ENOMEM);
}
static int am_archive_file_read(AmArchiveFile *file, int64_t offset, uint8_t *out, int count) {
    OVERLAPPED request = {0};
    request.Offset = (DWORD)(uint64_t)offset;
    request.OffsetHigh = (DWORD)((uint64_t)offset >> 32);
    request.hEvent = file->event;
    ResetEvent(file->event);
    DWORD actual = 0;
    if (!ReadFile(file->handle, out, (DWORD)count, &actual, &request)) {
        DWORD error = GetLastError();
        if (error == ERROR_HANDLE_EOF) return 0;
        if (error != ERROR_IO_PENDING || !GetOverlappedResult(file->handle, &request, &actual, TRUE)) return AVERROR(EIO);
    }
    atomic_fetch_add(&am_archive_metrics[2], actual);
    return (int)actual;
}
static void am_archive_file_close(AmArchiveFile *file) {
    if (file->handle) CloseHandle(file->handle);
    if (file->event) CloseHandle(file->event);
}
#else
typedef pthread_mutex_t AmArchiveMutex;
#define AM_ARCHIVE_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
static void am_archive_lock(AmArchiveMutex *m) { pthread_mutex_lock(m); }
static void am_archive_unlock(AmArchiveMutex *m) { pthread_mutex_unlock(m); }
typedef struct { uint64_t device, inode, modified, size; } AmArchiveIdentity;
typedef struct { int fd; AmArchiveIdentity identity; } AmArchiveFile;
static int am_archive_file_open(AmArchiveFile *file, const char *name) {
    file->fd = open(name, O_RDONLY);
    if (file->fd < 0) return AVERROR(errno);
    struct stat info;
    if (fstat(file->fd, &info) < 0) return AVERROR(errno);
#if defined(__APPLE__)
    uint64_t stamp = (uint64_t)info.st_mtimespec.tv_sec * 1000000000 + info.st_mtimespec.tv_nsec;
#else
    uint64_t stamp = (uint64_t)info.st_mtim.tv_sec * 1000000000 + info.st_mtim.tv_nsec;
#endif
    file->identity = (AmArchiveIdentity) {info.st_dev, info.st_ino, stamp, info.st_size};
    return 0;
}
static int am_archive_file_read(AmArchiveFile *file, int64_t offset, uint8_t *out, int count) {
    ssize_t actual;
    do { actual = pread(file->fd, out, count, offset); } while (actual < 0 && errno == EINTR);
    if (actual < 0) return AVERROR(errno);
    atomic_fetch_add(&am_archive_metrics[2], actual);
    return (int)actual;
}
static void am_archive_file_close(AmArchiveFile *file) { if (file->fd >= 0) close(file->fd); }
#endif

/* All zlib state allocations, including copies, are visible in telemetry. */
typedef union { max_align_t alignment; size_t size; } AmArchiveAllocation;
static voidpf am_archive_alloc(voidpf opaque, uInt items, uInt size) {
    (void)opaque;
    size_t bytes = (size_t)items * size;
    if (size && bytes / size != items) return NULL;
    if (bytes > SIZE_MAX - sizeof(AmArchiveAllocation)) return NULL;
    AmArchiveAllocation *allocation = malloc(sizeof(*allocation) + bytes);
    if (!allocation) return NULL;
    allocation->size = bytes;
    int64_t current = atomic_fetch_add(&am_archive_metrics[7], bytes) + bytes;
    int64_t peak = atomic_load(&am_archive_metrics[8]);
    while (current > peak && !atomic_compare_exchange_weak(&am_archive_metrics[8], &peak, current)) { }
    return allocation + 1;
}
static void am_archive_free(voidpf opaque, voidpf address) {
    (void)opaque;
    if (!address) return;
    AmArchiveAllocation *allocation = (AmArchiveAllocation *)address - 1;
    atomic_fetch_sub(&am_archive_metrics[7], allocation->size);
    free(allocation);
}

typedef struct { z_stream stream; int64_t position, compressed; } AmArchivePoint;
typedef struct AmArchiveIndex {
    struct AmArchiveIndex *next;
    AmArchiveIdentity identity;
    int64_t offset, compressed_length, length, stride;
    int refs;
    AmArchiveMutex mutex;
    AmArchivePoint *points[AM_ARCHIVE_POINTS];
} AmArchiveIndex;
static AmArchiveMutex am_archive_registry_mutex = AM_ARCHIVE_MUTEX_INIT;
static AmArchiveIndex *am_archive_indices;

typedef struct {
    AmArchiveFile file;
    AmArchiveIndex *index;
    int64_t offset, compressed_length, length, position, inflated_position, compressed_position;
    int method, initialized, ended;
    z_stream stream;
    uint8_t input[AM_ARCHIVE_BUFFER];
    uint8_t discard[AM_ARCHIVE_BUFFER];
} AmArchiveCursor;

static int am_archive_identity_equal(const AmArchiveIdentity *a, const AmArchiveIdentity *b) {
#if defined(_WIN32)
    return a->volume == b->volume && a->high == b->high && a->low == b->low && a->modified == b->modified && a->size == b->size;
#else
    return a->device == b->device && a->inode == b->inode && a->modified == b->modified && a->size == b->size;
#endif
}
static AmArchiveIndex *am_archive_index_acquire(AmArchiveCursor *cursor) {
    am_archive_lock(&am_archive_registry_mutex);
    AmArchiveIndex *index = am_archive_indices;
    while (index) {
        if (index->offset == cursor->offset && index->length == cursor->length && index->compressed_length == cursor->compressed_length
                && am_archive_identity_equal(&index->identity, &cursor->file.identity)) break;
        index = index->next;
    }
    if (!index) {
        index = calloc(1, sizeof(*index));
        if (index) {
            index->identity = cursor->file.identity;
            index->offset = cursor->offset;
            index->length = cursor->length;
            index->compressed_length = cursor->compressed_length;
            index->stride = cursor->length / AM_ARCHIVE_POINTS + (cursor->length % AM_ARCHIVE_POINTS != 0);
            if (index->stride < 1024 * 1024) index->stride = 1024 * 1024;
#if defined(_WIN32)
            InitializeSRWLock(&index->mutex);
#else
            pthread_mutex_init(&index->mutex, NULL);
#endif
            index->next = am_archive_indices;
            am_archive_indices = index;
            atomic_fetch_add(&am_archive_metrics[6], 1);
        }
    }
    if (index) index->refs++;
    am_archive_unlock(&am_archive_registry_mutex);
    return index;
}
static void am_archive_index_release(AmArchiveIndex *index) {
    if (!index) return;
    am_archive_lock(&am_archive_registry_mutex);
    if (--index->refs > 0) { am_archive_unlock(&am_archive_registry_mutex); return; }
    AmArchiveIndex **link = &am_archive_indices;
    while (*link && *link != index) link = &(*link)->next;
    if (*link) *link = index->next;
    am_archive_unlock(&am_archive_registry_mutex);
    for (int i = 0; i < AM_ARCHIVE_POINTS; i++) {
        if (index->points[i]) {
            inflateEnd(&index->points[i]->stream);
            free(index->points[i]);
            atomic_fetch_sub(&am_archive_metrics[5], 1);
        }
    }
#if !defined(_WIN32)
    pthread_mutex_destroy(&index->mutex);
#endif
    free(index);
    atomic_fetch_sub(&am_archive_metrics[6], 1);
}

static int am_archive_inflate_start(AmArchiveCursor *cursor) {
    if (cursor->initialized) inflateEnd(&cursor->stream);
    memset(&cursor->stream, 0, sizeof(cursor->stream));
    cursor->initialized = 0;
    cursor->stream.zalloc = am_archive_alloc;
    cursor->stream.zfree = am_archive_free;
    int status = inflateInit2(&cursor->stream, -MAX_WBITS);
    if (status != Z_OK) return AVERROR(ENOMEM);
    cursor->initialized = 1;
    cursor->ended = 0;
    cursor->inflated_position = cursor->compressed_position = 0;
    return 0;
}
static int am_archive_checkpoint(AmArchiveCursor *cursor) {
    AmArchiveIndex *index = cursor->index;
    if (!cursor->inflated_position || cursor->ended || cursor->inflated_position % index->stride) return 0;
    int64_t slot = cursor->inflated_position / index->stride;
    if (slot >= AM_ARCHIVE_POINTS) return 0;
    am_archive_lock(&index->mutex);
    if (!index->points[slot]) {
        AmArchivePoint *point = calloc(1, sizeof(*point));
        if (!point || inflateCopy(&point->stream, &cursor->stream) != Z_OK) {
            free(point);
            am_archive_unlock(&index->mutex);
            return AVERROR(ENOMEM);
        }
        point->position = cursor->inflated_position;
        point->compressed = cursor->compressed_position - cursor->stream.avail_in;
        /* Unconsumed bytes are reread from their exact file offset on restore.
         * Internal dictionary and bit accumulator stay in the copied state. */
        point->stream.next_in = Z_NULL;
        point->stream.avail_in = 0;
        point->stream.next_out = Z_NULL;
        point->stream.avail_out = 0;
        index->points[slot] = point;
        atomic_fetch_add(&am_archive_metrics[5], 1);
    }
    am_archive_unlock(&index->mutex);
    return 0;
}
static int am_archive_restore(AmArchiveCursor *cursor, int64_t target) {
    /* Sequential decode does not touch the shared checkpoint lock. */
    if (cursor->initialized && cursor->inflated_position == target) return 0;
    AmArchiveIndex *index = cursor->index;
    am_archive_lock(&index->mutex);
    int slot = (int)(target / index->stride);
    if (slot >= AM_ARCHIVE_POINTS) slot = AM_ARCHIVE_POINTS - 1;
    while (slot > 0 && !index->points[slot]) slot--;
    AmArchivePoint *point = index->points[slot];
    if (cursor->initialized && cursor->inflated_position <= target
            && (!point || cursor->inflated_position >= point->position)) {
        am_archive_unlock(&index->mutex);
        return 0;
    }
    if (!point) { am_archive_unlock(&index->mutex); return am_archive_inflate_start(cursor); }
    if (cursor->initialized) inflateEnd(&cursor->stream);
    memset(&cursor->stream, 0, sizeof(cursor->stream));
    cursor->initialized = 0;
    int status = inflateCopy(&cursor->stream, &point->stream);
    if (status == Z_OK) {
        cursor->initialized = 1;
        cursor->inflated_position = point->position;
        cursor->compressed_position = point->compressed;
        cursor->ended = 0;
    }
    am_archive_unlock(&index->mutex);
    return status == Z_OK ? 0 : AVERROR(ENOMEM);
}
static int am_archive_inflate(AmArchiveCursor *cursor, uint8_t *out, int length, int replay) {
    if (cursor->ended) return 0;
    int written = 0;
    while (written < length) {
        if (!cursor->stream.avail_in && cursor->compressed_position < cursor->compressed_length) {
            int amount = (int)FFMIN((int64_t)AM_ARCHIVE_BUFFER, cursor->compressed_length - cursor->compressed_position);
            int count = am_archive_file_read(&cursor->file, cursor->offset + cursor->compressed_position, cursor->input, amount);
            if (count <= 0) return count < 0 ? count : AVERROR(EIO);
            cursor->compressed_position += count;
            cursor->stream.next_in = cursor->input;
            cursor->stream.avail_in = count;
        }
        int64_t until_point = cursor->index->stride - cursor->inflated_position % cursor->index->stride;
        int amount = (int)FFMIN((int64_t)(length - written), until_point);
        cursor->stream.next_out = out + written;
        cursor->stream.avail_out = amount;
        uInt before_in = cursor->stream.avail_in;
        int status = inflate(&cursor->stream, Z_NO_FLUSH);
        int produced = amount - cursor->stream.avail_out;
        written += produced;
        cursor->inflated_position += produced;
        atomic_fetch_add(&am_archive_metrics[3], produced);
        if (replay) atomic_fetch_add(&am_archive_metrics[4], produced);
        if (cursor->inflated_position > cursor->length) return AVERROR(EIO);
        if (status == Z_STREAM_END) {
            cursor->ended = 1;
            if (cursor->inflated_position != cursor->length) return AVERROR(EIO);
            break;
        }
        if (status != Z_OK && status != Z_BUF_ERROR) return AVERROR(EIO);
        if (!produced && cursor->stream.avail_in == before_in) return AVERROR(EIO);
        int checkpoint_error = am_archive_checkpoint(cursor);
        if (checkpoint_error < 0) return checkpoint_error;
    }
    return written;
}
static int am_archive_read(void *opaque, uint8_t *out, int length) {
    AmArchiveCursor *cursor = opaque;
    atomic_fetch_add(&am_archive_metrics[10], 1);
    if (cursor->position >= cursor->length) return AVERROR_EOF;
    length = (int)FFMIN((int64_t)length, cursor->length - cursor->position);
    int count;
    if (!cursor->method) {
        count = am_archive_file_read(&cursor->file, cursor->offset + cursor->position, out, length);
    } else {
        count = am_archive_restore(cursor, cursor->position);
        if (count < 0) return count;
        while (cursor->inflated_position < cursor->position) {
            int amount = (int)FFMIN((int64_t)AM_ARCHIVE_BUFFER, cursor->position - cursor->inflated_position);
            count = am_archive_inflate(cursor, cursor->discard, amount, 1);
            if (count <= 0) return count < 0 ? count : AVERROR(EIO);
        }
        count = am_archive_inflate(cursor, out, length, 0);
    }
    if (count > 0) cursor->position += count;
    return count == 0 ? AVERROR(EIO) : count;
}
static int64_t am_archive_seek(void *opaque, int64_t offset, int whence) {
    AmArchiveCursor *cursor = opaque;
    atomic_fetch_add(&am_archive_metrics[9], 1);
    if (whence == AVSEEK_SIZE) return cursor->length;
    int64_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = cursor->position; break;
        case SEEK_END: base = cursor->length; break;
        default: return AVERROR(EINVAL);
    }
    if (offset < -base || offset > INT64_MAX - base) return AVERROR(EINVAL);
    cursor->position = base + offset;
    return cursor->position;
}
static void am_archive_release(void *opaque) {
    AmArchiveCursor *cursor = opaque;
    if (!cursor) return;
    if (cursor->initialized) inflateEnd(&cursor->stream);
    am_archive_index_release(cursor->index);
    am_archive_file_close(&cursor->file);
    free(cursor);
    atomic_fetch_add(&am_archive_metrics[1], 1);
    atomic_fetch_sub(&am_archive_metrics[11], 1);
}
static AmArchiveCursor *am_archive_open(const char *archive, int64_t offset, int64_t compressed, int64_t length, int method, int *error) {
    *error = AVERROR(EINVAL);
    if (!archive || !archive[0] || offset < 0 || compressed < 0 || length < 0 || offset > INT64_MAX - compressed
            || (method != 0 && method != 8) || (!method && compressed != length)) return NULL;
    AmArchiveCursor *cursor = calloc(1, sizeof(*cursor));
    if (!cursor) { *error = AVERROR(ENOMEM); return NULL; }
#if !defined(_WIN32)
    cursor->file.fd = -1;
#endif
    *error = am_archive_file_open(&cursor->file, archive);
    if (*error < 0 || (uint64_t)(offset + compressed) > cursor->file.identity.size) {
        if (*error >= 0) *error = AVERROR(EINVAL);
        am_archive_file_close(&cursor->file);
        free(cursor);
        return NULL;
    }
    cursor->offset = offset;
    cursor->compressed_length = compressed;
    cursor->length = length;
    cursor->method = method;
    if (method) {
        cursor->index = am_archive_index_acquire(cursor);
        if (!cursor->index) {
            *error = AVERROR(ENOMEM);
            am_archive_file_close(&cursor->file);
            free(cursor);
            return NULL;
        }
    }
    atomic_fetch_add(&am_archive_metrics[0], 1);
    atomic_fetch_add(&am_archive_metrics[11], 1);
    *error = 0;
    return cursor;
}

int64_t am_resource_io_metric(int metric) {
    return metric >= 0 && metric < 12 ? atomic_load(&am_archive_metrics[metric]) : 0;
}
#endif
