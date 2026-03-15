package com.gamescrcpy.companion;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.ImageView;

import java.io.DataInputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;

/**
 * 光标悬浮窗服务 — 仅处理光标显示，无需 MediaProjection
 *
 * TCP 端口 26758，协议:
 *   0x01 [float x][float y] — 更新光标位置 (归一化 0.0-1.0)
 *   0x03                    — 隐藏光标
 */
public class CursorService extends Service {
    public static final int PORT = 26758;
    private static final String CHANNEL_ID = "cursor_service";
    private static final long CURSOR_HIDE_DELAY = 5000;

    private WindowManager wm;
    private ImageView cursorView;
    private WindowManager.LayoutParams cursorParams;
    private Handler mainHandler;
    private int cursorSizePx;

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
        startTcpServer();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        running = false;
        try { if (serverSocket != null) serverSocket.close(); } catch (IOException ignored) {}
        if (cursorView != null) wm.removeView(cursorView);
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
                | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
                | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
            PixelFormat.TRANSLUCENT);
        cursorParams.gravity = Gravity.TOP | Gravity.START;
        wm.addView(cursorView, cursorParams);
    }

    private void updateCursor(float nx, float ny) {
        mainHandler.post(() -> {
            // 实时获取当前屏幕尺寸（跟随横竖屏旋转变化）
            Point realSize = new Point();
            wm.getDefaultDisplay().getRealSize(realSize);
            cursorParams.x = (int) (nx * realSize.x) - cursorSizePx / 2;
            cursorParams.y = (int) (ny * realSize.y) - cursorSizePx / 2;
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
                    new Thread(() -> handleClient(c), "cursor-client").start();
                }
            } catch (IOException e) {
                if (running) e.printStackTrace();
            }
        }, "cursor-server").start();
    }

    private void handleClient(Socket client) {
        try (DataInputStream in = new DataInputStream(client.getInputStream())) {
            while (running && !client.isClosed()) {
                int cmd = in.readUnsignedByte();
                switch (cmd) {
                    case 0x01: {
                        float x = in.readFloat();
                        float y = in.readFloat();
                        updateCursor(x, y);
                        break;
                    }
                    case 0x03: {
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

    // ─── 通知 ───

    private void createNotificationChannel() {
        NotificationChannel ch = new NotificationChannel(
            CHANNEL_ID, "Cursor Service", NotificationManager.IMPORTANCE_LOW);
        getSystemService(NotificationManager.class).createNotificationChannel(ch);
    }

    private Notification buildNotification() {
        return new Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("GS Companion")
            .setContentText("光标服务运行中 — 端口 " + PORT)
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .build();
    }
}
