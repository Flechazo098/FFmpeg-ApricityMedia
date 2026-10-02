/* Thin JNI adapter. Decoder and frame ownership stays in am_ffmpeg.c. */
#include <jni.h>
#define AM_BUILD_DLL
#include "am_ffmpeg.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "am_jni_io.h"

/* GetStringUTFChars produces modified UTF-8. FFmpeg paths require standard
 * UTF-8, including supplementary characters in Windows filenames. */
static char *to_utf8(JNIEnv *env, jstring value) {
    if (!value) return NULL;
    jsize length = (*env)->GetStringLength(env, value);
    const jchar *chars = (*env)->GetStringChars(env, value, NULL);
    if (!chars) return NULL;
    char *bytes = malloc((size_t)length * 3 + 1);
    if (!bytes) {
        (*env)->ReleaseStringChars(env, value, chars);
        jclass type = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
        if (type) (*env)->ThrowNew(env, type, "JNI UTF-8 path allocation failed");
        return NULL;
    }
    size_t cursor = 0;
    for (jsize i = 0; i < length; i++) {
        unsigned cp = chars[i];
        if (cp == 0) {
            free(bytes);
            (*env)->ReleaseStringChars(env, value, chars);
            jclass type = (*env)->FindClass(env, "java/lang/IllegalArgumentException");
            if (type) (*env)->ThrowNew(env, type, "Native strings cannot contain NUL");
            return NULL;
        }
        if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < length
                && chars[i + 1] >= 0xdc00 && chars[i + 1] <= 0xdfff) {
            cp = 0x10000 + ((cp - 0xd800) << 10) + (chars[++i] - 0xdc00);
        } else if (cp >= 0xd800 && cp <= 0xdfff) {
            cp = 0xfffd;
        }
        if (cp < 0x80) {
            bytes[cursor++] = (char)cp;
        } else if (cp < 0x800) {
            bytes[cursor++] = (char)(0xc0 | (cp >> 6));
            bytes[cursor++] = (char)(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            bytes[cursor++] = (char)(0xe0 | (cp >> 12));
            bytes[cursor++] = (char)(0x80 | ((cp >> 6) & 0x3f));
            bytes[cursor++] = (char)(0x80 | (cp & 0x3f));
        } else {
            bytes[cursor++] = (char)(0xf0 | (cp >> 18));
            bytes[cursor++] = (char)(0x80 | ((cp >> 12) & 0x3f));
            bytes[cursor++] = (char)(0x80 | ((cp >> 6) & 0x3f));
            bytes[cursor++] = (char)(0x80 | (cp & 0x3f));
        }
    }
    bytes[cursor] = 0;
    (*env)->ReleaseStringChars(env, value, chars);
    return bytes;
}

static jstring from_utf8(JNIEnv *env, const char *value) {
    if (!value) value = "";
    size_t length = strlen(value);
    if (length > INT_MAX) return NULL;
    jbyteArray bytes = (*env)->NewByteArray(env, (jsize)length);
    if (!bytes) return NULL;
    (*env)->SetByteArrayRegion(env, bytes, 0, (jsize)length, (const jbyte *)value);
    jclass type = (*env)->FindClass(env, "java/lang/String");
    if (!type) return NULL;
    jmethodID constructor = (*env)->GetMethodID(env, type, "<init>", "([BLjava/lang/String;)V");
    if (!constructor) return NULL;
    jstring charset = (*env)->NewStringUTF(env, "UTF-8");
    if (!charset) return NULL;
    return (jstring)(*env)->NewObject(env, type, constructor, bytes, charset);
}

static int has_array(JNIEnv *env, jarray array, jsize required) {
    return array && (*env)->GetArrayLength(env, array) >= required;
}

JNIEXPORT void JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_init(JNIEnv *env, jclass type) {
    (void)env; (void)type;
    am_init();
}

JNIEXPORT jstring JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_lastError(JNIEnv *env, jclass type) {
    (void)env; (void)type;
    return from_utf8(env, am_last_error());
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoOpen(JNIEnv *env, jclass type, jstring path, jint targetWidth, jint targetHeight, jdouble maxFps, jint networkTimeoutMs, jint networkBufferKb, jboolean networkReconnect, jboolean hwDecodeEnabled, jboolean hwNvdecEnabled, jstring hwPreferred) {
    (void)env; (void)type;
    char *utfPath = to_utf8(env, path);
    if ((*env)->ExceptionCheck(env)) return 0;
    char *utfPreferred = to_utf8(env, hwPreferred);
    if ((*env)->ExceptionCheck(env)) { free(utfPath); return 0; }
    jlong result = (jlong)am_video_open(utfPath, targetWidth, targetHeight, maxFps,
        networkTimeoutMs, networkBufferKb, networkReconnect, hwDecodeEnabled,
        hwNvdecEnabled, utfPreferred ? utfPreferred : "auto");
    free(utfPath);
    free(utfPreferred);
    return result;
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoReadFrame(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    return (jlong)am_video_read_frame((uint64_t)decoderHandle);
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_resourceIoMetric(JNIEnv *env, jclass type, jint metric) {
    (void)env; (void)type;
    return (jlong)am_resource_io_metric(metric);
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoOpenRegion(JNIEnv *env, jclass type,
        jstring path, jint width, jint height, jdouble fps, jint timeout, jint buffer, jboolean reconnect,
        jboolean hardware, jboolean nvdec, jstring preferred, jstring archive, jlong offset, jlong compressed, jlong length, jint method) {
    (void)type;
    char *name = to_utf8(env, path);
    if ((*env)->ExceptionCheck(env)) return 0;
    char *backend = to_utf8(env, preferred);
    if ((*env)->ExceptionCheck(env)) { free(name); return 0; }
    char *file = to_utf8(env, archive);
    if ((*env)->ExceptionCheck(env)) { free(name); free(backend); return 0; }
    jlong result = (jlong)am_video_open_region(name, width, height, fps, timeout, buffer, reconnect, hardware, nvdec,
        backend ? backend : "auto", file, offset, compressed, length, method);
    free(name); free(backend); free(file);
    return result;
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_audioOpenRegion(JNIEnv *env, jclass type,
        jstring path, jint timeout, jint buffer, jboolean reconnect, jstring archive, jlong offset, jlong compressed, jlong length, jint method) {
    (void)type;
    char *name = to_utf8(env, path);
    if ((*env)->ExceptionCheck(env)) return 0;
    char *file = to_utf8(env, archive);
    if ((*env)->ExceptionCheck(env)) { free(name); return 0; }
    jlong result = (jlong)am_audio_open_region(name, timeout, buffer, reconnect, file, offset, compressed, length, method);
    free(name); free(file);
    return result;
}

#include "am_jni_union_path.h"

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoOpenInput(JNIEnv *env, jclass type,
        jstring path, jint width, jint height, jdouble fps, jint timeout, jint buffer, jboolean reconnect,
        jboolean hardware, jboolean nvdec, jstring preferred, jobject callback) {
    (void)type;
    char *name = to_utf8(env, path);
    if ((*env)->ExceptionCheck(env)) return 0;
    char *backend = to_utf8(env, preferred);
    if ((*env)->ExceptionCheck(env)) { free(name); return 0; }
    AmJniInput *input = am_jni_input_create(env, callback);
    if (!input) { free(name); free(backend); return 0; }
    jlong result = (jlong)am_video_open_io(name, width, height, fps, timeout, buffer, reconnect, hardware, nvdec,
                                          backend ? backend : "auto", input, am_jni_input_read,
                                          am_jni_input_seek, am_jni_input_release);
    free(name);
    free(backend);
    return result;
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_audioOpenInput(JNIEnv *env, jclass type,
        jstring path, jint timeout, jint buffer, jboolean reconnect, jobject callback) {
    (void)type;
    char *name = to_utf8(env, path);
    if ((*env)->ExceptionCheck(env)) return 0;
    AmJniInput *input = am_jni_input_create(env, callback);
    if (!input) { free(name); return 0; }
    jlong result = (jlong)am_audio_open_io(name, timeout, buffer, reconnect, input,
                                          am_jni_input_read, am_jni_input_seek, am_jni_input_release);
    free(name);
    return result;
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameRetainSource(JNIEnv *env, jclass type, jlong frame) {
    (void)env; (void)type;
    return (jlong)am_video_frame_retain_source((uint64_t)frame);
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameCreatePresentation(JNIEnv *env, jclass type, jlong source, jint width, jint height, jlong generation) {
    (void)env; (void)type;
    return (jlong)am_video_frame_create_presentation((uint64_t)source, width, height, (uint64_t)generation);
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetPresentationInfo(JNIEnv *env, jclass type, jlong frame, jlongArray info) {
    (void)env; (void)type;
    if (!has_array(env, info, 3)) return 0;
    int64_t values[3] = {0};
    jint result = am_video_frame_get_presentation_info((uint64_t)frame, values);
    jlong output[3];
    for (int i = 0; i < 3; i++) output[i] = (jlong)values[i];
    (*env)->SetLongArrayRegion(env, info, 0, 3, output);
    return result;
}

JNIEXPORT jboolean JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoSetPresentationSize(JNIEnv *env, jclass type, jlong decoderHandle, jint physicalWidth, jint physicalHeight, jlong generation) {
    (void)env; (void)type;
    return am_video_set_presentation_size((uint64_t)decoderHandle, physicalWidth, physicalHeight, (uint64_t)generation) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetInfo(JNIEnv *env, jclass type, jlong frameHandle, jlongArray info) {
    (void)env; (void)type;
    if (!has_array(env, info, 4)) return 0;
    int64_t values[4] = {0};
    jint result = am_video_frame_get_info((uint64_t)frameHandle, values);
    jlong output[4];
    for (int i = 0; i < 4; i++) output[i] = (jlong)values[i];
    (*env)->SetLongArrayRegion(env, info, 0, 4, output);
    return result;
}

JNIEXPORT jobject JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetPixels(JNIEnv *env, jclass type, jlong frameHandle, jint width, jint height) {
    (void)env; (void)type;
    int64_t info[4] = {0};
    int actualSize = am_video_frame_get_info((uint64_t)frameHandle, info);
    if (width <= 0 || height <= 0 || width != info[0] || height != info[1] || actualSize <= 0) return NULL;
    void *pixels = am_video_frame_get_pixels((uint64_t)frameHandle);
    return pixels ? (*env)->NewDirectByteBuffer(env, pixels, actualSize) : NULL;
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetPixelFormat(JNIEnv *env, jclass type, jlong frameHandle) {
    (void)env; (void)type;
    return (jint)am_video_frame_get_pixel_format((uint64_t)frameHandle);
}

JNIEXPORT jboolean JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameIsGpuFrame(JNIEnv *env, jclass type, jlong frameHandle) {
    (void)env; (void)type;
    return am_video_frame_is_gpu((uint64_t)frameHandle) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetGpuBackendTag(JNIEnv *env, jclass type, jlong frameHandle) {
    (void)env; (void)type;
    return (jint)am_video_frame_get_gpu_backend((uint64_t)frameHandle);
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetGpuHandle(JNIEnv *env, jclass type, jlong frameHandle) {
    (void)env; (void)type;
    return (jlong)am_video_frame_get_gpu_handle((uint64_t)frameHandle);
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetGpuSubresource(JNIEnv *env, jclass type, jlong frameHandle) {
    (void)env; (void)type;
    return (jint)am_video_frame_get_gpu_subresource((uint64_t)frameHandle);
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetGpuSurfaceInfo(JNIEnv *env, jclass type, jlong frameHandle, jintArray info) {
    (void)env; (void)type;
    if (!has_array(env, info, 2)) return 0;
    int values[2] = {0};
    jint result = am_video_frame_get_gpu_surface_info((uint64_t)frameHandle, values);
    jint output[2];
    for (int i = 0; i < 2; i++) output[i] = (jint)values[i];
    (*env)->SetIntArrayRegion(env, info, 0, 2, output);
    return result;
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetPlaneCount(JNIEnv *env, jclass type, jlong frameHandle) {
    (void)env; (void)type;
    return (jint)am_video_frame_get_plane_count((uint64_t)frameHandle);
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetPlaneInfo(JNIEnv *env, jclass type, jlong frameHandle, jint planeIndex, jintArray info) {
    (void)env; (void)type;
    if (!has_array(env, info, 3)) return 0;
    int values[3] = {0};
    jint result = am_video_frame_get_plane_info((uint64_t)frameHandle, planeIndex, values);
    jint output[3];
    for (int i = 0; i < 3; i++) output[i] = (jint)values[i];
    (*env)->SetIntArrayRegion(env, info, 0, 3, output);
    return result;
}

JNIEXPORT jobject JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetPlaneBuffer(JNIEnv *env, jclass type, jlong frameHandle, jint planeIndex, jlong dataSize) {
    (void)env; (void)type;
    int info[3] = {0};
    int actualSize = am_video_frame_get_plane_info((uint64_t)frameHandle, planeIndex, info);
    if (dataSize <= 0 || dataSize > actualSize) return NULL;
    void *pixels = am_video_frame_get_plane_buffer((uint64_t)frameHandle, planeIndex);
    return pixels ? (*env)->NewDirectByteBuffer(env, pixels, dataSize) : NULL;
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetColorInfo(JNIEnv *env, jclass type, jlong frameHandle, jintArray info) {
    (void)env; (void)type;
    if (!has_array(env, info, 4)) return 0;
    int values[4] = {0};
    jint result = am_video_frame_get_color_info((uint64_t)frameHandle, values);
    jint output[4];
    for (int i = 0; i < 4; i++) output[i] = (jint)values[i];
    (*env)->SetIntArrayRegion(env, info, 0, 4, output);
    return result;
}

JNIEXPORT jstring JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameGetSourcePixelFormat(JNIEnv *env, jclass type, jlong frameHandle) {
    (void)env; (void)type;
    return from_utf8(env, am_video_frame_get_source_pixel_format((uint64_t)frameHandle));
}

JNIEXPORT void JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoFrameRelease(JNIEnv *env, jclass type, jlong frameHandle) {
    (void)env; (void)type;
    am_video_frame_release((uint64_t)frameHandle);
}

JNIEXPORT void JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoRewind(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    am_video_rewind((uint64_t)decoderHandle);
}

JNIEXPORT jboolean JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoSeekMs(JNIEnv *env, jclass type, jlong decoderHandle, jlong targetMs) {
    (void)env; (void)type;
    return am_video_seek_ms((uint64_t)decoderHandle, (uint64_t)targetMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoGetDurationMs(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    return (jlong)am_video_get_duration_ms((uint64_t)decoderHandle);
}

JNIEXPORT jboolean JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoIsHardwareDecode(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    return am_video_is_hardware_decode((uint64_t)decoderHandle) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoGetHardwareBackend(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    return from_utf8(env, am_video_get_hardware_backend((uint64_t)decoderHandle));
}

JNIEXPORT jstring JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoGetHardwareProbeMessage(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    return from_utf8(env, am_video_get_hardware_probe_message((uint64_t)decoderHandle));
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoGetHardwareDeviceHandle(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    return (jlong)am_video_get_hardware_device_handle((uint64_t)decoderHandle);
}

JNIEXPORT void JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_videoClose(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    am_video_close((uint64_t)decoderHandle);
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_audioOpen(JNIEnv *env, jclass type, jstring path, jint networkTimeoutMs, jint networkBufferKb, jboolean networkReconnect) {
    (void)env; (void)type;
    char *utfPath = to_utf8(env, path);
    if ((*env)->ExceptionCheck(env)) return 0;
    jlong result = (jlong)am_audio_open(utfPath, networkTimeoutMs, networkBufferKb, networkReconnect);
    free(utfPath);
    return result;
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_audioReadPcm(JNIEnv *env, jclass type, jlong decoderHandle, jbyteArray buffer, jint offset, jint length) {
    (void)env; (void)type;
    if (!buffer) {
        jclass error = (*env)->FindClass(env, "java/lang/NullPointerException");
        if (error) (*env)->ThrowNew(env, error, "PCM buffer");
        return -2;
    }
    jsize capacity = (*env)->GetArrayLength(env, buffer);
    if (offset < 0 || length < 0 || offset > capacity || length > capacity - offset) {
        jclass error = (*env)->FindClass(env, "java/lang/IndexOutOfBoundsException");
        if (error) (*env)->ThrowNew(env, error, "PCM offset and length");
        return -2;
    }
    if (length == 0) return 0;
    /* Do not pin the Java heap across decoding or network I/O. */
    jbyte *data = (*env)->GetByteArrayElements(env, buffer, NULL);
    if (!data) return -2;
    jint result = am_audio_read_pcm((uint64_t)decoderHandle, (uint8_t *)data, offset, length);
    (*env)->ReleaseByteArrayElements(env, buffer, data, result > 0 ? 0 : JNI_ABORT);
    return result;
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_audioSampleRate(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    return (jint)am_audio_sample_rate((uint64_t)decoderHandle);
}

JNIEXPORT jint JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_audioChannels(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    return (jint)am_audio_channels((uint64_t)decoderHandle);
}

JNIEXPORT void JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_audioRewind(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    am_audio_rewind((uint64_t)decoderHandle);
}

JNIEXPORT jboolean JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_audioSeekMs(JNIEnv *env, jclass type, jlong decoderHandle, jlong targetMs) {
    (void)env; (void)type;
    return am_audio_seek_ms((uint64_t)decoderHandle, (uint64_t)targetMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_audioGetDurationMs(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    return (jlong)am_audio_get_duration_ms((uint64_t)decoderHandle);
}

JNIEXPORT void JNICALL Java_cc_sighs_apricitymedia_jni_ApricityMediaNative_audioClose(JNIEnv *env, jclass type, jlong decoderHandle) {
    (void)env; (void)type;
    am_audio_close((uint64_t)decoderHandle);
}
