#ifndef AM_JNI_UNION_PATH_H
#define AM_JNI_UNION_PATH_H

/* JNI invokes the loader's own selection algorithm without module-opening
 * flags, object-layout access, or Java reflection. This compatibility boundary
 * is intentionally limited to the known SecureJarHandler UnionPath contract.
 * It resolves metadata once, never participates in decode/read hot paths. */
JNIEXPORT jobject JNICALL Java_cc_sighs_apricitymedia_minecraft_runtime_NativeResourcePathBridge_resolve(JNIEnv *env, jclass type, jobject path) {
    (void)type;
    if (!path) return NULL;
    jclass path_type = (*env)->GetObjectClass(env, path);
    jmethodID get_fs = (*env)->GetMethodID(env, path_type, "getFileSystem", "()Ljava/nio/file/FileSystem;");
    if (!get_fs) return NULL;
    jobject fs = (*env)->CallObjectMethod(env, path, get_fs);
    if ((*env)->ExceptionCheck(env) || !fs) return NULL;
    jclass fs_type = (*env)->GetObjectClass(env, fs);
    jmethodID provider_method = (*env)->GetMethodID(env, fs_type, "provider", "()Ljava/nio/file/spi/FileSystemProvider;");
    if (!provider_method) return NULL;
    jobject provider = (*env)->CallObjectMethod(env, fs, provider_method);
    if ((*env)->ExceptionCheck(env) || !provider) return NULL;
    jclass provider_type = (*env)->GetObjectClass(env, provider);
    jmethodID scheme_method = (*env)->GetMethodID(env, provider_type, "getScheme", "()Ljava/lang/String;");
    if (!scheme_method) return NULL;
    jstring scheme = (*env)->CallObjectMethod(env, provider, scheme_method);
    if ((*env)->ExceptionCheck(env) || !scheme) return NULL;
    char *scheme_utf = to_utf8(env, scheme);
    if (!scheme_utf) return NULL;
    int is_union = strcmp(scheme_utf, "union") == 0;
    free(scheme_utf);
    if (!is_union) return path;
    jmethodID select = (*env)->GetMethodID(env, fs_type, "findFirstFiltered", "(Lcpw/mods/niofs/union/UnionPath;)Ljava/util/Optional;");
    if (!select) {
        (*env)->ExceptionClear(env);
        jclass error = (*env)->FindClass(env, "java/io/IOException");
        if (error) (*env)->ThrowNew(env, error, "Unsupported SecureJarHandler union resource selection contract");
        return NULL;
    }
    jobject selected = (*env)->CallObjectMethod(env, fs, select, path);
    if ((*env)->ExceptionCheck(env) || !selected) return NULL;
    jclass optional = (*env)->GetObjectClass(env, selected);
    jmethodID present_method = (*env)->GetMethodID(env, optional, "isPresent", "()Z");
    jmethodID get = (*env)->GetMethodID(env, optional, "get", "()Ljava/lang/Object;");
    if (!present_method || !get) return NULL;
    jboolean present = (*env)->CallBooleanMethod(env, selected, present_method);
    if ((*env)->ExceptionCheck(env)) return NULL;
    if (!present) {
        jclass error = (*env)->FindClass(env, "java/io/FileNotFoundException");
        if (error) (*env)->ThrowNew(env, error, "Union resource is absent or filtered by its owner");
        return NULL;
    }
    return (*env)->CallObjectMethod(env, selected, get);
}

#endif
