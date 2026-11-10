package com.gamescrcpy.server.device;

import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.ScreenshotFormat;
import com.gamescrcpy.protocol.Messages.RequestScreenshot;
import com.gamescrcpy.protocol.Messages.Screenshot;
import com.gamescrcpy.server.Log;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class ScreenshotService implements AutoCloseable {

    public static final int MAX_PNG_BYTES = 48 * 1024 * 1024;
    public static final int MAX_SIDE = 0xFFFF;

    public interface Platform {
        DisplaySnapshot display(int displayId) throws BulkRequests.Failure;

        byte[] png(DisplaySnapshot display) throws BulkRequests.Failure;
    }

    private final Platform platform;
    private final BulkRequests bulk;
    private final ExecutorService captures = Executors.newSingleThreadExecutor(runnable -> {
        final Thread thread = new Thread(runnable, "gs-screenshot");
        thread.setDaemon(true);
        return thread;
    });

    public ScreenshotService(Platform platform, BulkRequests bulk) {
        this.platform = platform;
        this.bulk = bulk;
    }

    public void capture(RequestScreenshot request) {
        final long requestId = request.requestId;
        final int displayId = (int) request.displayId;
        bulk.submit(requestId, captures, () -> screenshot(requestId, displayId));
    }

    @Override
    public void close() {
        captures.shutdownNow();
    }

    private Screenshot screenshot(long requestId, int displayId) throws BulkRequests.Failure {
        final String name = "display " + Integer.toUnsignedString(displayId);
        final DisplaySnapshot display = platform.display(displayId);
        if (display == null) {
            throw new BulkRequests.Failure(ErrorCode.NOT_FOUND, name + " does not exist");
        }
        final int width = display.width();
        final int height = display.height();
        if (width < 1 || width > MAX_SIDE || height < 1 || height > MAX_SIDE) {
            throw new BulkRequests.Failure(ErrorCode.INTERNAL_ERROR, name + " is " + width + "x" + height
                    + ", outside what a screenshot can describe");
        }
        final long started = System.nanoTime();
        final byte[] png = platform.png(display);
        if (png.length > MAX_PNG_BYTES) {
            throw new BulkRequests.Failure(ErrorCode.INTERNAL_ERROR, "the screenshot of " + name + " takes "
                    + png.length + " bytes, over the limit of " + MAX_PNG_BYTES);
        }
        Log.i("screenshot of " + name + ": " + width + "x" + height + ", " + png.length + " bytes of PNG in "
                + (System.nanoTime() - started) / 1_000_000 + " ms");
        final Screenshot screenshot = new Screenshot();
        screenshot.requestId = requestId;
        screenshot.width = width;
        screenshot.height = height;
        screenshot.format = ScreenshotFormat.PNG;
        screenshot.data = png;
        return screenshot;
    }
}
