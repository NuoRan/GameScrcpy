package com.gamescrcpy.server.wrappers;

import android.annotation.SuppressLint;
import android.graphics.Bitmap;
import android.graphics.PixelFormat;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.server.device.BulkRequests;
import com.gamescrcpy.server.device.DisplaySnapshot;
import com.gamescrcpy.server.device.ScreenshotService;
import java.io.ByteArrayOutputStream;
import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.function.IntPredicate;

public final class Screenshots implements ScreenshotService.Platform {

    private static final long FIRST_IMAGE_MILLIS = 2_000;
    private static final int MAX_IMAGES = 2;
    private static final int BYTES_PER_PIXEL = 4;

    private final Displays displays;
    private final IntPredicate independent;

    private Screenshots(Displays displays, IntPredicate independent) {
        this.displays = displays;
        this.independent = independent;
    }

    public static Screenshots create(IntPredicate independent) throws PlatformException {
        return new Screenshots(Displays.get(), independent);
    }

    @Override
    public DisplaySnapshot display(int displayId) throws BulkRequests.Failure {
        try {
            return independent.test(displayId) ? displays.snapshotOfAny(displayId) : displays.snapshot(displayId);
        } catch (PlatformException e) {
            throw new BulkRequests.Failure(ErrorCode.INTERNAL_ERROR, e.getMessage(), e);
        }
    }

    @Override
    public byte[] png(DisplaySnapshot display) throws BulkRequests.Failure {
        final String name = "display " + Integer.toUnsignedString(display.displayId());
        Bitmap bitmap = null;
        try {
            bitmap = Bitmap.createBitmap(display.width(), display.height(), Bitmap.Config.ARGB_8888);
            grab(display, name, bitmap);
            bitmap.setHasAlpha(false);
            final ByteArrayOutputStream png = new ByteArrayOutputStream();
            if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, png)) {
                throw new BulkRequests.Failure(ErrorCode.INTERNAL_ERROR, "the picture of " + name
                        + " could not be encoded as PNG");
            }
            return png.toByteArray();
        } catch (RuntimeException | OutOfMemoryError e) {
            throw new BulkRequests.Failure(ErrorCode.INTERNAL_ERROR, "cannot capture " + name + ": " + e, e);
        } finally {
            if (bitmap != null) {
                bitmap.recycle();
            }
        }
    }

    private static void grab(DisplaySnapshot display, String name, Bitmap bitmap) throws BulkRequests.Failure {
        if (Looper.myLooper() == null) {
            Looper.prepare();
        }
        final HandlerThread thread = new HandlerThread("gs-screenshot-image");
        thread.start();
        ImageReader reader = null;
        DisplayMirror mirror = null;
        Image image = null;
        try {
            final CountDownLatch arrived = new CountDownLatch(1);
            reader = reader(display.width(), display.height());
            reader.setOnImageAvailableListener(available -> arrived.countDown(), new Handler(thread.getLooper()));
            mirror = DisplayMirror.create(display, display.width(), display.height(), reader.getSurface());
            if (!arrived.await(FIRST_IMAGE_MILLIS, TimeUnit.MILLISECONDS)) {
                throw new BulkRequests.Failure(ErrorCode.INTERNAL_ERROR, name + " showed no picture within "
                        + FIRST_IMAGE_MILLIS + " ms");
            }
            image = reader.acquireLatestImage();
            if (image == null) {
                throw new BulkRequests.Failure(ErrorCode.INTERNAL_ERROR, "the picture of " + name
                        + " was gone before it could be read");
            }
            bitmap.copyPixelsFromBuffer(pixels(image));
        } catch (PlatformException e) {
            throw new BulkRequests.Failure(ErrorCode.INTERNAL_ERROR, e.getMessage(), e);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new BulkRequests.Failure(ErrorCode.INTERNAL_ERROR, "the screenshot of " + name
                    + " was interrupted", e);
        } finally {
            if (image != null) {
                image.close();
            }
            if (mirror != null) {
                mirror.close();
            }
            if (reader != null) {
                reader.close();
            }
            thread.quitSafely();
        }
    }

    @SuppressLint("WrongConstant")
    private static ImageReader reader(int width, int height) {
        return ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, MAX_IMAGES);
    }

    private static ByteBuffer pixels(Image image) {
        final Image.Plane plane = image.getPlanes()[0];
        final ByteBuffer source = plane.getBuffer();
        final int width = image.getWidth();
        final int height = image.getHeight();
        final int pixelStride = plane.getPixelStride();
        final int rowStride = plane.getRowStride();
        final int rowBytes = width * BYTES_PER_PIXEL;
        if (pixelStride == BYTES_PER_PIXEL && rowStride == rowBytes) {
            return source;
        }
        final byte[] packed = new byte[rowBytes * height];
        final Buffer cursor = source;
        for (int y = 0; y < height; ++y) {
            if (pixelStride == BYTES_PER_PIXEL) {
                cursor.position(y * rowStride);
                source.get(packed, y * rowBytes, rowBytes);
                continue;
            }
            for (int x = 0; x < width; ++x) {
                final int from = y * rowStride + x * pixelStride;
                final int to = y * rowBytes + x * BYTES_PER_PIXEL;
                for (int channel = 0; channel < BYTES_PER_PIXEL; ++channel) {
                    packed[to + channel] = source.get(from + channel);
                }
            }
        }
        return ByteBuffer.wrap(packed);
    }
}
