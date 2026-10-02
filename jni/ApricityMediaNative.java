package cc.sighs.apricitymedia.jni;

import java.nio.ByteBuffer;

/** JNI declarations. The runtime bootstrap loads the library before use. */
public final class ApricityMediaNative {
    private ApricityMediaNative() {}

    public static native void init();

    public static native String lastError();

    public static native long videoOpen(String path, int targetWidth, int targetHeight, double maxFps, int networkTimeoutMs, int networkBufferKb, boolean networkReconnect, boolean hwDecodeEnabled, boolean hwNvdecEnabled, String hwPreferred);

    public static native long videoReadFrame(long decoderHandle);

    public static native long videoFrameRetainSource(long frame);

    public static native long videoFrameCreatePresentation(long source, int width, int height, long generation);

    public static native int videoFrameGetPresentationInfo(long frame, long[] info);

    public static native boolean videoSetPresentationSize(long decoderHandle, int physicalWidth, int physicalHeight, long generation);

    public static native int videoFrameGetInfo(long frameHandle, long[] info);

    public static native ByteBuffer videoFrameGetPixels(long frameHandle, int width, int height);

    public static native int videoFrameGetPixelFormat(long frameHandle);

    public static native boolean videoFrameIsGpuFrame(long frameHandle);

    public static native int videoFrameGetGpuBackendTag(long frameHandle);

    public static native long videoFrameGetGpuHandle(long frameHandle);

    public static native int videoFrameGetGpuSubresource(long frameHandle);

    public static native int videoFrameGetGpuSurfaceInfo(long frameHandle, int[] info);

    public static native int videoFrameGetPlaneCount(long frameHandle);

    public static native int videoFrameGetPlaneInfo(long frameHandle, int planeIndex, int[] info);

    public static native ByteBuffer videoFrameGetPlaneBuffer(long frameHandle, int planeIndex, long dataSize);

    public static native int videoFrameGetColorInfo(long frameHandle, int[] info);

    public static native String videoFrameGetSourcePixelFormat(long frameHandle);

    public static native void videoFrameRelease(long frameHandle);

    public static native void videoRewind(long decoderHandle);

    public static native boolean videoSeekMs(long decoderHandle, long targetMs);

    public static native long videoGetDurationMs(long decoderHandle);

    public static native boolean videoIsHardwareDecode(long decoderHandle);

    public static native String videoGetHardwareBackend(long decoderHandle);

    public static native String videoGetHardwareProbeMessage(long decoderHandle);

    public static native long videoGetHardwareDeviceHandle(long decoderHandle);

    public static native void videoClose(long decoderHandle);

    public static native long audioOpen(String path, int networkTimeoutMs, int networkBufferKb, boolean networkReconnect);

    public static native int audioReadPcm(long decoderHandle, byte[] buffer, int offset, int length);

    public static native int audioSampleRate(long decoderHandle);

    public static native int audioChannels(long decoderHandle);

    public static native void audioRewind(long decoderHandle);

    public static native boolean audioSeekMs(long decoderHandle, long targetMs);

    public static native long audioGetDurationMs(long decoderHandle);

    public static native void audioClose(long decoderHandle);

    // Standalone header-generation declarations use Object for the callback.
    // The shipped Java binding narrows it to core MediaInput. Both use jobject.
    public static native long videoOpenInput(String path, int width, int height, double fps,
            int timeout, int buffer, boolean reconnect, boolean hardware, boolean nvdec,
            String preferred, Object input);
    public static native long audioOpenInput(String path, int timeout, int buffer,
            boolean reconnect, Object input);
    public static native long videoOpenRegion(String path, int width, int height, double fps,
            int timeout, int buffer, boolean reconnect, boolean hardware, boolean nvdec,
            String preferred, String archive, long offset, long compressedLength, long length, int compression);
    public static native long audioOpenRegion(String path, int timeout, int buffer, boolean reconnect,
            String archive, long offset, long compressedLength, long length, int compression);
    public static native long resourceIoMetric(int metric);
}
