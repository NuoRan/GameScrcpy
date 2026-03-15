package com.gamescrcpy.companion;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.util.DisplayMetrics;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * 截屏服务 — 接收 PC 截屏请求，返回 JPEG 后关闭连接
 *
 * TCP 端口 26759，协议:
 *   PC→Phone: 0x02         — 请求截屏
 *   Phone→PC: 0x82 [int32 len][JPEG] — 截屏响应
 *   截屏完成后自动关闭当前连接，服务保持运行等待下一次请求
 */
public class ScreenshotService extends Service {
    public static final int PORT = 26759;
    private static final String CHANNEL_ID = "screenshot_service";

    /** 静态回调，供 MainActivity 监听服务结束 */
    public interface OnCompleteListener {
        void onScreenshotComplete(boolean success);
    }
    public static volatile OnCompleteListener completeListener;

    private Handler mainHandler;
    private MediaProjection mediaProjection;
    private ServerSocket serverSocket;
    private volatile boolean running = true;

    @Override
    public void onCreate() {
        super.onCreate();
        mainHandler = new Handler(Looper.getMainLooper());
        createNotificationChannel();
        startForeground(2, buildNotification());
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && mediaProjection == null) {
            int rc = intent.getIntExtra("resultCode", 0);
            Intent data = intent.getParcelableExtra("data");
            if (data != null) {
                MediaProjectionManager mpm = getSystemService(MediaProjectionManager.class);
                mediaProjection = mpm.getMediaProjection(rc, data);
                if (Build.VERSION.SDK_INT >= 34 && mediaProjection != null) {
                    mediaProjection.registerCallback(new MediaProjection.Callback() {}, mainHandler);
                }
            }
        }
        if (serverSocket == null) startTcpServer();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        running = false;
        try { if (serverSocket != null) serverSocket.close(); } catch (IOException ignored) {}
        if (mediaProjection != null) mediaProjection.stop();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    // ─── TCP 服务器 ───

    private void startTcpServer() {
        new Thread(() -> {
            try {
                serverSocket = new ServerSocket(PORT);
                while (running) {
                    Socket c = serverSocket.accept();
                    new Thread(() -> handleClient(c), "screenshot-client").start();
                }
            } catch (IOException e) {
                if (running) e.printStackTrace();
            }
        }, "screenshot-server").start();
    }

    private void handleClient(Socket client) {
        boolean success = false;
        try (DataInputStream in = new DataInputStream(client.getInputStream());
             DataOutputStream out = new DataOutputStream(client.getOutputStream())) {
            int cmd = in.readUnsignedByte();
            if (cmd == 0x02) {
                byte[] jpeg = captureScreen();
                out.writeByte(0x82);
                out.writeInt(jpeg != null ? jpeg.length : 0);
                if (jpeg != null) {
                    out.write(jpeg);
                    success = true;
                }
                out.flush();
            }
        } catch (IOException ignored) {
        } finally {
            try { client.close(); } catch (IOException ignored) {}
            // 通知 MainActivity 并自动停止服务
            final boolean ok = success;
            if (completeListener != null) {
                mainHandler.post(() -> {
                    if (completeListener != null) completeListener.onScreenshotComplete(ok);
                });
            }
            stopSelf();
        }
    }

    // ─── 截屏 ───

    private byte[] captureScreen() {
        if (mediaProjection == null) return null;
        try {
            return captureScreenImpl();
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }

    private byte[] captureScreenImpl() {
        DisplayMetrics dm = getResources().getDisplayMetrics();
        int w = dm.widthPixels, h = dm.heightPixels, dpi = dm.densityDpi;

        HandlerThread ht = new HandlerThread("screencap");
        ht.start();
        Handler capHandler = new Handler(ht.getLooper());

        ImageReader reader = ImageReader.newInstance(w, h, PixelFormat.RGBA_8888, 2);
        CountDownLatch latch = new CountDownLatch(1);
        byte[][] result = {null};

        reader.setOnImageAvailableListener(r -> {
            Image img = r.acquireLatestImage();
            if (img != null && result[0] == null) {
                try {
                    Image.Plane p = img.getPlanes()[0];
                    ByteBuffer buf = p.getBuffer();
                    int rowStride = p.getRowStride();
                    int pixStride = p.getPixelStride();
                    int padding = rowStride - pixStride * w;
                    int bw = w + padding / Math.max(pixStride, 1);

                    Bitmap bmp = Bitmap.createBitmap(bw, h, Bitmap.Config.ARGB_8888);
                    bmp.copyPixelsFromBuffer(buf);
                    if (bw != w) bmp = Bitmap.createBitmap(bmp, 0, 0, w, h);

                    ByteArrayOutputStream baos = new ByteArrayOutputStream();
                    bmp.compress(Bitmap.CompressFormat.JPEG, 85, baos);
                    bmp.recycle();
                    result[0] = baos.toByteArray();
                } finally {
                    img.close();
                    latch.countDown();
                }
            }
        }, capHandler);

        VirtualDisplay vd = mediaProjection.createVirtualDisplay("cap",
            w, h, dpi, DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            reader.getSurface(), null, capHandler);

        try { latch.await(5, TimeUnit.SECONDS); } catch (InterruptedException ignored) {}

        vd.release();
        reader.close();
        ht.quitSafely();
        return result[0];
    }

    // ─── 通知 ───

    private void createNotificationChannel() {
        NotificationChannel ch = new NotificationChannel(
            CHANNEL_ID, "Screenshot Service", NotificationManager.IMPORTANCE_LOW);
        getSystemService(NotificationManager.class).createNotificationChannel(ch);
    }

    private Notification buildNotification() {
        return new Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("GS Companion")
            .setContentText("截屏服务运行中 — 端口 " + PORT)
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .build();
    }
}
