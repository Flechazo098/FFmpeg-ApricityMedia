#ifndef AM_JNI_IO_H
#define AM_JNI_IO_H

/* JNI owns only a global callback reference. Java decoder leases own closing
 * the MediaInput itself, after the native close call and final frame leases. */
typedef struct {
    JavaVM *vm;
    jobject input;
    jmethodID read;
    jmethodID seek;
} AmJniInput;

static JNIEnv *am_jni_input_env(AmJniInput *input, int *attached) {
    JNIEnv *env = NULL;
    *attached = 0;
    jint status = (*input->vm)->GetEnv(input->vm, (void **)&env, JNI_VERSION_1_8);
    if (status == JNI_EDETACHED) {
        if ((*input->vm)->AttachCurrentThread(input->vm, (void **)&env, NULL) != JNI_OK) return NULL;
        *attached = 1;
    } else if (status != JNI_OK) return NULL;
    return env;
}

static int am_jni_input_read(void *opaque, uint8_t *buffer, int length) {
    AmJniInput *input = opaque;
    int attached;
    JNIEnv *env = am_jni_input_env(input, &attached);
    if (!env) return -5;
    jobject bytes = (*env)->NewDirectByteBuffer(env, buffer, length);
    jint result = bytes ? (*env)->CallIntMethod(env, input->input, input->read, bytes) : -5;
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        result = -5;
    }
    if (bytes) (*env)->DeleteLocalRef(env, bytes);
    if (attached) (*input->vm)->DetachCurrentThread(input->vm);
    return result;
}

static int64_t am_jni_input_seek(void *opaque, int64_t offset, int whence) {
    AmJniInput *input = opaque;
    int attached;
    JNIEnv *env = am_jni_input_env(input, &attached);
    if (!env) return -5;
    jlong result = (*env)->CallLongMethod(env, input->input, input->seek, (jlong)offset, (jint)whence);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        result = -5;
    }
    if (attached) (*input->vm)->DetachCurrentThread(input->vm);
    return result;
}

static void am_jni_input_release(void *opaque) {
    AmJniInput *input = opaque;
    if (!input) return;
    int attached;
    JNIEnv *env = am_jni_input_env(input, &attached);
    if (env) (*env)->DeleteGlobalRef(env, input->input);
    if (attached) (*input->vm)->DetachCurrentThread(input->vm);
    free(input);
}

static AmJniInput *am_jni_input_create(JNIEnv *env, jobject callback) {
    if (!callback) return NULL;
    AmJniInput *input = calloc(1, sizeof(*input));
    if (!input) {
        jclass type = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
        if (type) (*env)->ThrowNew(env, type, "AVIO callback allocation failed");
        return NULL;
    }
    if ((*env)->GetJavaVM(env, &input->vm) != JNI_OK) { free(input); return NULL; }
    jclass type = (*env)->GetObjectClass(env, callback);
    input->read = (*env)->GetMethodID(env, type, "read", "(Ljava/nio/ByteBuffer;)I");
    input->seek = (*env)->GetMethodID(env, type, "seek", "(JI)J");
    (*env)->DeleteLocalRef(env, type);
    if (!input->read || !input->seek || (*env)->ExceptionCheck(env)) { free(input); return NULL; }
    input->input = (*env)->NewGlobalRef(env, callback);
    if (!input->input) { free(input); return NULL; }
    return input;
}

#endif
