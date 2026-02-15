package com.genymobile.scrcpy.control;

import com.genymobile.scrcpy.device.Point;
import com.genymobile.scrcpy.device.Position;
import com.genymobile.scrcpy.device.Size;
import com.genymobile.scrcpy.util.AffineMatrix;

public final class PositionMapper {

    private final Size videoSize;
    private final Size deviceSize; // [新增] 保存设备真实分辨率
    private final AffineMatrix videoToDeviceMatrix;

    // [修改] 构造函数增加 deviceSize
    public PositionMapper(Size videoSize, Size deviceSize, AffineMatrix videoToDeviceMatrix) {
        this.videoSize = videoSize;
        this.deviceSize = deviceSize;
        this.videoToDeviceMatrix = videoToDeviceMatrix;
    }

    public static PositionMapper create(Size videoSize, AffineMatrix filterTransform, Size targetSize) {
        boolean convertToPixels = !videoSize.equals(targetSize) || filterTransform != null;
        AffineMatrix transform = filterTransform;
        if (convertToPixels) {
            AffineMatrix inputTransform = AffineMatrix.ndcFromPixels(videoSize);
            AffineMatrix outputTransform = AffineMatrix.ndcToPixels(targetSize);
            transform = outputTransform.multiply(transform).multiply(inputTransform);
        }

        // [修改] 创建时传入 targetSize (即设备真实尺寸)
        return new PositionMapper(videoSize, targetSize, transform);
    }

    public Size getVideoSize() {
        return videoSize;
    }

    public Size getDeviceSize() {
        return deviceSize;
    }

    public Point map(Position position) {
        Size clientVideoSize = position.getScreenSize();

        // 直接控制模式：客户端发送设备真实尺寸时直接使用坐标
        if (deviceSize.equals(clientVideoSize)) {
            return position.getPoint();
        }

        // 2. 如果客户端发送的尺寸等于视频流尺寸 -> 传统模式 (Relative Control)
        // 这对应原版逻辑，需要通过矩阵将视频坐标映射回设备坐标。
        if (videoSize.equals(clientVideoSize)) {
            Point point = position.getPoint();
            if (videoToDeviceMatrix != null) {
                point = videoToDeviceMatrix.apply(point);
            }
            return point;
        }

        // 3. 尺寸都不匹配 (可能是设备旋转了但客户端还没更新，或者数据错误) -> 丢弃
        return null;
    }
}
