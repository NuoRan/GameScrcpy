package com.gamescrcpy.companion;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.os.Build;
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
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.ImageView;

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
 * 悬浮光标服务 + TCP 命令服务器
 *
 * 协议 (大端序):
 *   PC → Phone:
 *     0x01 [float x][float y]  — 更新光标位置 (归一化 0.0-1.0)
 *     0x02                     — 请求截屏
 *     0x03                     — 隐藏光标
 *   Phone → PC:
 *     0x82 [int32 len][JPEG]   — 截屏响应
 */
public class OverlayService extends Service {
    public static final int PORT = 26758;
    private static final String CHANNEL_ID = "cursor_overlay";
    private static final long CURSOR_HIDE_DELAY = 5000;

    private WindowManager wm;
    private ImageView cursorView;
    private WindowManager.LayoutParams cursorParams;
    private Handler mainHandler;
    private int cursorSizePx;

    private MediaProjection mediaProjection;
    private ServerSocket serverSocket;
    private volatile boolean running = true;

    private final Runnable autoHide = () -> {
        if (cursorView != null) cursorView.setVisibility(View.GONE);
    };

    @Override
    public void onCreate() {
        super.onCreate();
        mainHandler = new Handler(Looper.getMainLooper());
        wm = getSystemService(WindowManager.class);
        cursorSizePx = (int) (40 * getResources().getDisplayMetrics().density);

        createNotificationChannel();
        startForeground(1, buildNotification());
        createCursorOverlay();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && mediaProjection == null) {
            int rc = intent.getIntExtra("resultCode", 0);
            Intent data = intent.getParcelableExtra("data");
            if (data != null) {
                MediaProjectionManager mpm = getSystemService(MediaProjectionManager.class);
                mediaProjection = mpm.getMediaProjection(rc, data);
                // Android 14+ 要求先注册回调才能 createVirtualDisplay
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
        if (cursorView != null) wm.removeView(cursorView);
        if (mediaProjection != null) mediaProjection.stop();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    // ─── 悬浮光标 ───

    private void createCursorOverlay() {
        cursorView = new ImageView(this);
        cursorView.setImageResource(R.drawable.ic_cursor);
        cursorView.setVisibility(View.GONE);

        cursorParams = new WindowManager.LayoutParams(
            cursorSizePx, cursorSizePx,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT);
        cursorParams.gravity = Gravity.TOP | Gravity.START;
        wm.addView(cursorView, cursorParams);
    }

    private void updateCursor(float nx, float ny) {
        mainHandler.post(() -> {
            DisplayMetrics dm = getResources().getDisplayMetrics();
            cursorParams.x = (int) (nx * dm.widthPixels) - cursorSizePx / 2;
            cursorParams.y = (int) (ny * dm.heightPixels) - cursorSizePx / 2;
            cursorView.setVisibility(View.VISIBLE);
            wm.updateViewLayout(cursorView, cursorParams);

            mainHandler.removeCallbacks(autoHide);
            mainHandler.postDelayed(autoHide, CURSOR_HIDE_DELAY);
        });
    }

    // ─── TCP 服务器 ───

    private void startTcpServer() {
        new Thread(() -> {
            try {
                serverSocket = new ServerSocket(PORT);
                while (running) {
                    Socket c = serverSocket.accept();
                    new Thread(() -> handleClient(c), "companion-client").start();
                }
            } catch (IOException e) {
                if (running) e.printStackTrace();
            }
        }, "companion-server").start();
    }

    private void handleClient(Socket client) {
        try (DataInputStream in = new DataInputStream(client.getInputStream());
             DataOutputStream out = new DataOutputStream(client.getOutputStream())) {
            while (running && !client.isClosed()) {
                int cmd = in.readUnsignedByte();
                switch (cmd) {
                    case 0x01: {  // cursor position
                        float x = in.readFloat();
                        float y = in.readFloat();
                        updateCursor(x, y);
                        break;
                    }
                    case 0x02: {  // screenshot
                        byte[] jpeg = captureScreen();
                        out.writeByte(0x82);
                        out.writeInt(jpeg != null ? jpeg.length : 0);
                        if (jpeg != null) out.write(jpeg);
                        out.flush();
                        break;
                    }
                    case 0x03: {  // hide cursor
                        mainHandler.post(() -> cursorView.setVisibility(View.GONE));
                        break;
                    }
                }
            }
        } catch (IOException ignored) {
        } finally {
            try { client.close(); } catch (IOException ignored) {}
            mainHandler.post(() -> {
                if (cursorView != null) cursorView.setVisibility(View.GONE);
            });
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

        try { latch.await(3, TimeUnit.SECONDS); } catch (InterruptedException ignored) {}

        vd.release();
        reader.close();
        ht.quitSafely();
        return result[0];
    }

    // ─── 通知 ───

    private void createNotificationChannel() {
        NotificationChannel ch = new NotificationChannel(
            CHANNEL_ID, "Cursor Overlay", NotificationManager.IMPORTANCE_LOW);
        getSystemService(NotificationManager.class).createNotificationChannel(ch);
    }

    private Notification buildNotification() {
        return new Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("GS Companion")
            .setContentText("Cursor overlay — port " + PORT)
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .build();
    }
}
