package com.genymobile.scrcpy.device;

import com.genymobile.scrcpy.control.ControlChannel;
import com.genymobile.scrcpy.auxiliary.TcpAuxChannel;
import com.genymobile.scrcpy.auxiliary.IAuxChannel;
import com.genymobile.scrcpy.util.IO;
import com.genymobile.scrcpy.util.StringUtils;

import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;

import java.io.Closeable;
import java.io.FileDescriptor;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

public final class DesktopConnection implements Closeable {

    private static final int DEVICE_NAME_FIELD_LENGTH = 64;

    private static final String SOCKET_NAME_PREFIX = "scrcpy";

    private final LocalSocket videoSocket;
    private final FileDescriptor videoFd;

    private final LocalSocket audioSocket;
    private final FileDescriptor audioFd;

    private final LocalSocket controlSocket;
    private final ControlChannel controlChannel;

    private final LocalSocket auxSocket;
    private final IAuxChannel auxChannel;

    private DesktopConnection(LocalSocket videoSocket, LocalSocket audioSocket, LocalSocket controlSocket, LocalSocket auxSocket) throws IOException {
        this.videoSocket = videoSocket;
        this.audioSocket = audioSocket;
        this.controlSocket = controlSocket;
        this.auxSocket = auxSocket;

        videoFd = videoSocket != null ? videoSocket.getFileDescriptor() : null;
        audioFd = audioSocket != null ? audioSocket.getFileDescriptor() : null;
        controlChannel = controlSocket != null ? new ControlChannel(controlSocket) : null;
        auxChannel = auxSocket != null ? new TcpAuxChannel(auxSocket) : null;
    }

    private static LocalSocket connect(String abstractName) throws IOException {
        LocalSocket localSocket = new LocalSocket();
        localSocket.connect(new LocalSocketAddress(abstractName));
        return localSocket;
    }

    private static String getSocketName(int scid) {
        if (scid == -1) {
            return SOCKET_NAME_PREFIX;
        }
        return SOCKET_NAME_PREFIX + String.format("_%08x", scid);
    }

    public static DesktopConnection open(int scid, boolean tunnelForward, boolean video, boolean audio, boolean control, boolean sendDummyByte)
            throws IOException {
        String baseSocketName = getSocketName(scid);
        String videoSocketName = baseSocketName + "_video";
        String audioSocketName = baseSocketName + "_audio";
        String controlSocketName = baseSocketName + "_control";
        String auxSocketName = baseSocketName + "_aux";

        LocalSocket videoSocket = null;
        LocalSocket audioSocket = null;
        LocalSocket controlSocket = null;
        LocalSocket auxSocket = null;

        try {
            if (tunnelForward) {
                if (video) {
                    try (LocalServerSocket videoServerSocket = new LocalServerSocket(videoSocketName)) {
                        videoSocket = videoServerSocket.accept();
                        if (sendDummyByte) {
                            videoSocket.getOutputStream().write(0);
                        }
                    }
                }

                if (audio) {
                    try (LocalServerSocket audioServerSocket = new LocalServerSocket(audioSocketName)) {
                        audioSocket = audioServerSocket.accept();
                        if (sendDummyByte) {
                            audioSocket.getOutputStream().write(0);
                        }
                    }
                }

                if (control) {
                    try (LocalServerSocket controlServerSocket = new LocalServerSocket(controlSocketName)) {
                        controlSocket = controlServerSocket.accept();
                        if (sendDummyByte) {
                            controlSocket.getOutputStream().write(0);
                        }
                    }
                }

                // 辅助通道（始终创建）
                try (LocalServerSocket auxServerSocket = new LocalServerSocket(auxSocketName)) {
                    auxSocket = auxServerSocket.accept();
                    if (sendDummyByte) {
                        auxSocket.getOutputStream().write(0);
                    }
                }
            } else {
                if (video) {
                    videoSocket = connect(videoSocketName);
                }
                if (audio) {
                    audioSocket = connect(audioSocketName);
                }
                if (control) {
                    controlSocket = connect(controlSocketName);
                }
                // 辅助通道
                auxSocket = connect(auxSocketName);
            }
        } catch (IOException | RuntimeException e) {
            if (videoSocket != null) {
                videoSocket.close();
            }
            if (audioSocket != null) {
                audioSocket.close();
            }
            if (controlSocket != null) {
                controlSocket.close();
            }
            if (auxSocket != null) {
                auxSocket.close();
            }
            throw e;
        }

        return new DesktopConnection(videoSocket, audioSocket, controlSocket, auxSocket);
    }

    private LocalSocket getFirstSocket() {
        if (videoSocket != null) {
            return videoSocket;
        }
        if (audioSocket != null) {
            return audioSocket;
        }
        return controlSocket;
    }

    public void shutdown() throws IOException {
        if (videoSocket != null) {
            videoSocket.shutdownInput();
            videoSocket.shutdownOutput();
        }
        if (audioSocket != null) {
            audioSocket.shutdownInput();
            audioSocket.shutdownOutput();
        }
        if (controlSocket != null) {
            controlSocket.shutdownInput();
            controlSocket.shutdownOutput();
        }
        if (auxSocket != null) {
            auxSocket.shutdownInput();
            auxSocket.shutdownOutput();
        }
    }

    public void close() throws IOException {
        if (videoSocket != null) {
            videoSocket.close();
        }
        if (audioSocket != null) {
            audioSocket.close();
        }
        if (controlSocket != null) {
            controlSocket.close();
        }
        if (auxSocket != null) {
            auxSocket.close();
        }
    }

    public void sendDeviceMeta(String deviceName) throws IOException {
        byte[] buffer = new byte[DEVICE_NAME_FIELD_LENGTH];

        byte[] deviceNameBytes = deviceName.getBytes(StandardCharsets.UTF_8);
        int len = StringUtils.getUtf8TruncationIndex(deviceNameBytes, DEVICE_NAME_FIELD_LENGTH - 1);
        System.arraycopy(deviceNameBytes, 0, buffer, 0, len);

        FileDescriptor fd = getFirstSocket().getFileDescriptor();
        IO.writeFully(fd, buffer, 0, buffer.length);
    }

    public FileDescriptor getVideoFd() {
        return videoFd;
    }

    public FileDescriptor getAudioFd() {
        return audioFd;
    }

    public ControlChannel getControlChannel() {
        return controlChannel;
    }

    public IAuxChannel getAuxChannel() {
        return auxChannel;
    }
}
