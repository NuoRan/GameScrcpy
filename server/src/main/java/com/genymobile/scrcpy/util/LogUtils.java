package com.genymobile.scrcpy.util;

import com.genymobile.scrcpy.AndroidVersions;
import com.genymobile.scrcpy.device.DeviceApp;
import com.genymobile.scrcpy.device.DisplayInfo;
import com.genymobile.scrcpy.device.Size;
import com.genymobile.scrcpy.video.VideoCodec;
import com.genymobile.scrcpy.wrappers.DisplayManager;
import com.genymobile.scrcpy.wrappers.ServiceManager;

import android.annotation.TargetApi;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.os.Build;

import java.util.Collections;
import java.util.List;
import java.util.Objects;

public final class LogUtils {

    private LogUtils() {
        // not instantiable
    }

    public static String buildVideoEncoderListMessage() {
        StringBuilder builder = new StringBuilder("List of video encoders:");
        MediaCodecList codecList = new MediaCodecList(MediaCodecList.REGULAR_CODECS);
        for (Codec codec : VideoCodec.values()) {
            MediaCodecInfo[] encoders = CodecUtils.getEncoders(codecList, codec.getMimeType());
            for (MediaCodecInfo info : encoders) {
                int lineStart = builder.length();
                builder.append("\n    --video-codec=").append(codec.getName());
                builder.append(" --video-encoder=").append(info.getName());
                if (Build.VERSION.SDK_INT >= AndroidVersions.API_29_ANDROID_10) {
                    int lineLength = builder.length() - lineStart;
                    final int column = 70;
                    if (lineLength < column) {
                        int padding = column - lineLength;
                        builder.append(String.format("%" + padding + "s", " "));
                    }
                    builder.append(" (").append(getHwCodecType(info)).append(')');
                    if (info.isVendor()) {
                        builder.append(" [vendor]");
                    }
                    if (info.isAlias()) {
                        builder.append(" (alias for ").append(info.getCanonicalName()).append(')');
                    }
                }
            }
        }
        return builder.toString();
    }

    @TargetApi(AndroidVersions.API_29_ANDROID_10)
    private static String getHwCodecType(MediaCodecInfo info) {
        if (info.isSoftwareOnly()) {
            return "sw";
        }
        if (info.isHardwareAccelerated()) {
            return "hw";
        }
        return "hybrid";
    }

    public static String buildDisplayListMessage() {
        StringBuilder builder = new StringBuilder("List of displays:");
        DisplayManager displayManager = ServiceManager.getDisplayManager();
        int[] displayIds = displayManager.getDisplayIds();
        if (displayIds == null || displayIds.length == 0) {
            builder.append("\n    (none)");
        } else {
            for (int id : displayIds) {
                builder.append("\n    --display-id=").append(id).append("    (");
                DisplayInfo displayInfo = displayManager.getDisplayInfo(id);
                if (displayInfo != null) {
                    Size size = displayInfo.getSize();
                    builder.append(size.getWidth()).append("x").append(size.getHeight());
                } else {
                    builder.append("size unknown");
                }
                builder.append(")");
            }
        }
        return builder.toString();
    }

    public static String buildAppListMessage(String title, List<DeviceApp> apps) {
        StringBuilder builder = new StringBuilder(title);

        // Sort by:
        //  1. system flag (system apps are before non-system apps)
        //  2. name
        //  3. package name
        Collections.sort(apps, (thisApp, otherApp) -> {
            // System apps first
            int cmp = -Boolean.compare(thisApp.isSystem(), otherApp.isSystem());
            if (cmp != 0) {
                return cmp;
            }

            cmp = Objects.compare(thisApp.getName(), otherApp.getName(), String::compareTo);
            if (cmp != 0) {
                return cmp;
            }

            return Objects.compare(thisApp.getPackageName(), otherApp.getPackageName(), String::compareTo);
        });

        final int column = 30;
        for (DeviceApp app : apps) {
            String name = app.getName();
            int padding = column - name.length();
            builder.append("\n ");
            if (app.isSystem()) {
                builder.append("* ");
            } else {
                builder.append("- ");
            }
            builder.append(name);
            if (padding > 0) {
                builder.append(String.format("%" + padding + "s", " "));
            } else {
                builder.append("\n   ").append(String.format("%" + column + "s", " "));
            }
            builder.append(" ").append(app.getPackageName());
        }

        return builder.toString();
    }
}
