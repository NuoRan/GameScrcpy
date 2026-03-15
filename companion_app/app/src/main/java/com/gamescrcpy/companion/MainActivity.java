package com.gamescrcpy.companion;

import android.app.Activity;
import android.content.Intent;
import android.media.projection.MediaProjectionManager;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.widget.Button;
import android.widget.TextView;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Enumeration;

public class MainActivity extends Activity {
    private static final int REQ_OVERLAY = 1;
    private static final int REQ_CAPTURE = 2;

    private TextView tvStatus;
    private Button btnCursor;
    private Button btnScreenshot;
    private boolean cursorRunning = false;
    private boolean screenshotRunning = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        tvStatus = findViewById(R.id.tvStatus);
        btnCursor = findViewById(R.id.btnCursor);
        btnScreenshot = findViewById(R.id.btnScreenshot);

        updateStatus();

        // 功能一: 光标服务 (仅需悬浮窗权限)
        btnCursor.setOnClickListener(v -> {
            if (!cursorRunning) {
                startCursorService();
            } else {
                stopCursorService();
            }
        });

        // 功能二: 截屏服务 (需录屏权限, 截完自动关闭)
        btnScreenshot.setOnClickListener(v -> {
            if (!screenshotRunning) {
                startScreenshotService();
            } else {
                stopScreenshotService();
            }
        });
    }

    // ─── 光标服务 ───

    private void startCursorService() {
        if (!Settings.canDrawOverlays(this)) {
            startActivityForResult(
                new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName())),
                REQ_OVERLAY);
            return;
        }
        Intent intent = new Intent(this, CursorService.class);
        startForegroundService(intent);
        cursorRunning = true;
        btnCursor.setText("停止光标服务");
        updateStatus();
    }

    private void stopCursorService() {
        stopService(new Intent(this, CursorService.class));
        cursorRunning = false;
        btnCursor.setText("启动光标服务");
        updateStatus();
    }

    // ─── 截屏服务 ───

    private void startScreenshotService() {
        // 注册完成回调（服务截屏后自动停止）
        ScreenshotService.completeListener = success -> runOnUiThread(() -> {
            screenshotRunning = false;
            btnScreenshot.setText("启动截屏服务");
            updateStatus();
        });
        MediaProjectionManager mpm = getSystemService(MediaProjectionManager.class);
        startActivityForResult(mpm.createScreenCaptureIntent(), REQ_CAPTURE);
    }

    private void stopScreenshotService() {
        ScreenshotService.completeListener = null;
        stopService(new Intent(this, ScreenshotService.class));
        screenshotRunning = false;
        btnScreenshot.setText("启动截屏服务");
        updateStatus();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQ_OVERLAY) {
            if (Settings.canDrawOverlays(this)) {
                startCursorService();
            }
        } else if (requestCode == REQ_CAPTURE && resultCode == RESULT_OK && data != null) {
            Intent intent = new Intent(this, ScreenshotService.class);
            intent.putExtra("resultCode", resultCode);
            intent.putExtra("data", data);
            startForegroundService(intent);
            screenshotRunning = true;
            btnScreenshot.setText("停止截屏服务");
            updateStatus();
        }
    }

    private void updateStatus() {
        String ip = getLocalIp();
        DisplayMetrics dm = getResources().getDisplayMetrics();
        int w = dm.widthPixels, h = dm.heightPixels;
        int shortSide = Math.min(w, h), longSide = Math.max(w, h);
        StringBuilder sb = new StringBuilder();
        sb.append("IP: ").append(ip);
        sb.append("\n光标端口: ").append(CursorService.PORT);
        sb.append("\n截屏端口: ").append(ScreenshotService.PORT);
        sb.append("\n竖屏分辨率: ").append(shortSide).append(" x ").append(longSide);
        sb.append("\n横屏分辨率: ").append(longSide).append(" x ").append(shortSide);
        sb.append("\n\n光标服务: ").append(cursorRunning ? "运行中" : "未启动");
        sb.append("\n截屏服务: ").append(screenshotRunning ? "等待截屏请求..." : "未启动");
        tvStatus.setText(sb.toString());
    }

    private String getLocalIp() {
        try {
            Enumeration<NetworkInterface> nis = NetworkInterface.getNetworkInterfaces();
            while (nis.hasMoreElements()) {
                Enumeration<InetAddress> addrs = nis.nextElement().getInetAddresses();
                while (addrs.hasMoreElements()) {
                    InetAddress addr = addrs.nextElement();
                    if (!addr.isLoopbackAddress() && addr instanceof Inet4Address) {
                        return addr.getHostAddress();
                    }
                }
            }
        } catch (Exception ignored) {}
        return "unknown";
    }
}
