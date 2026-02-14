package com.genymobile.scrcpy.kcp;

/**
 * FEC 前向纠错编解码器 (Java 服务端)
 * Forward Error Correction Codec (Java server side)
 *
 * [超低延迟优化] 基于 XOR 的简单 FEC，用于 KCP 传输层。
 * 每 groupSize 个数据包生成 1 个 XOR 校验包。
 * 当丢失 1 个数据包时，可通过其余包 + FEC 包恢复，无需等待重传。
 *
 * 编码格式:
 *   [1B type] [1B groupId] [1B index] [1B groupSize] [2B originalLen] [payload...]
 *   type: 0x01 = 数据包, 0x02 = FEC 校验包
 */
public final class FecCodec {

    public static final int FEC_HEADER_SIZE = 6;
    public static final byte FEC_TYPE_DATA = 0x01;
    public static final byte FEC_TYPE_PARITY = 0x02;

    /**
     * FEC 编码器 - 发送端使用
     */
    public static class FecEncoder {
        private final int groupSize;
        private final int maxPacketSize;
        private byte groupId = 0;
        private int index = 0;
        private int maxParityLen = 0;
        private final byte[] parityBuf;

        // 预分配编码缓冲区
        private final byte[] encodeBuf;

        public FecEncoder(int groupSize, int maxPacketSize) {
            this.groupSize = groupSize;
            this.maxPacketSize = maxPacketSize;
            this.parityBuf = new byte[maxPacketSize];
            this.encodeBuf = new byte[maxPacketSize + FEC_HEADER_SIZE];
        }

        public FecEncoder(int groupSize) {
            this(groupSize, 1400);
        }

        public FecEncoder() {
            this(10, 1400);
        }

        /**
         * 编码一个数据包
         *
         * @param data   原始数据
         * @param offset 数据偏移
         * @param len    数据长度
         * @param output 输出回调接口
         */
        public synchronized void encode(byte[] data, int offset, int len, OutputCallback output) {
            if (data == null || len <= 0 || output == null) return;

            if (len > maxPacketSize - FEC_HEADER_SIZE) {
                // 超大包直接透传
                output.onOutput(data, offset, len);
                return;
            }

            // 构建 FEC 数据包
            encodeBuf[0] = FEC_TYPE_DATA;
            encodeBuf[1] = groupId;
            encodeBuf[2] = (byte) index;
            encodeBuf[3] = (byte) groupSize;
            encodeBuf[4] = (byte) ((len >> 8) & 0xFF);
            encodeBuf[5] = (byte) (len & 0xFF);
            System.arraycopy(data, offset, encodeBuf, FEC_HEADER_SIZE, len);

            // 输出数据包
            output.onOutput(encodeBuf, 0, FEC_HEADER_SIZE + len);

            // 更新 XOR 校验
            int paddedLen = len + 2; // 包含 originalLen 字段
            // XOR originalLen
            parityBuf[0] ^= encodeBuf[4];
            parityBuf[1] ^= encodeBuf[5];
            // XOR payload
            for (int i = 0; i < len; i++) {
                parityBuf[2 + i] ^= data[offset + i];
            }
            if (paddedLen > maxParityLen) {
                maxParityLen = paddedLen;
            }

            index++;

            // 一组完成，发射 FEC 校验包
            if (index >= groupSize) {
                encodeBuf[0] = FEC_TYPE_PARITY;
                encodeBuf[1] = groupId;
                encodeBuf[2] = (byte) groupSize;
                encodeBuf[3] = (byte) groupSize;
                encodeBuf[4] = (byte) ((maxParityLen >> 8) & 0xFF);
                encodeBuf[5] = (byte) (maxParityLen & 0xFF);
                System.arraycopy(parityBuf, 0, encodeBuf, FEC_HEADER_SIZE, maxParityLen);

                output.onOutput(encodeBuf, 0, FEC_HEADER_SIZE + maxParityLen);

                // 重置组
                groupId++;
                index = 0;
                maxParityLen = 0;
                java.util.Arrays.fill(parityBuf, (byte) 0);
            }
        }

        public interface OutputCallback {
            void onOutput(byte[] data, int offset, int len);
        }
    }

    /**
     * FEC 解码器 - 接收端使用
     */
    public static class FecDecoder {
        private static final int MAX_GROUPS = 4;
        private final GroupSlot[] groups = new GroupSlot[MAX_GROUPS];
        private int nextSlot = 0;

        public FecDecoder() {
            for (int i = 0; i < MAX_GROUPS; i++) {
                groups[i] = new GroupSlot();
            }
        }

        /**
         * 解码一个 FEC 包
         */
        public synchronized void decode(byte[] data, int offset, int len, OutputCallback output) {
            if (data == null || len < FEC_HEADER_SIZE || output == null) {
                if (output != null && data != null && len > 0) {
                    output.onOutput(data, offset, len);
                }
                return;
            }

            byte type = data[offset];
            byte gid = data[offset + 1];
            byte idx = data[offset + 2];
            byte gs = data[offset + 3];

            if (type != FEC_TYPE_DATA && type != FEC_TYPE_PARITY) {
                output.onOutput(data, offset, len);
                return;
            }

            if (gs <= 0 || gs > 32) return;

            FecGroup group = getOrCreateGroup(gid, gs);
            int payloadLen = len - FEC_HEADER_SIZE;

            if (type == FEC_TYPE_DATA) {
                int originalLen = ((data[offset + 4] & 0xFF) << 8) | (data[offset + 5] & 0xFF);
                if (originalLen <= 0 || originalLen > payloadLen) return;

                if (idx < gs && !group.received[idx]) {
                    group.received[idx] = true;
                    group.receivedCount++;
                    group.packets[idx] = new byte[payloadLen];
                    System.arraycopy(data, offset + FEC_HEADER_SIZE, group.packets[idx], 0, payloadLen);
                    group.originalLens[idx] = originalLen;
                }

                // 输出原始数据
                output.onOutput(data, offset + FEC_HEADER_SIZE + 2, originalLen);

            } else if (type == FEC_TYPE_PARITY) {
                int parityLen = ((data[offset + 4] & 0xFF) << 8) | (data[offset + 5] & 0xFF);
                if (parityLen <= 0 || parityLen > payloadLen) return;

                group.hasParity = true;
                group.parityData = new byte[parityLen];
                System.arraycopy(data, offset + FEC_HEADER_SIZE, group.parityData, 0, parityLen);
                group.parityLen = parityLen;

                tryRecover(group, output);
            }

            // 数据包到达后也检查是否可恢复
            if (group.hasParity && group.receivedCount == gs - 1) {
                tryRecover(group, output);
            }
        }

        private void tryRecover(FecGroup group, OutputCallback output) {
            if (group.recovered || !group.hasParity) return;
            if (group.receivedCount < group.groupSize - 1) return;
            if (group.receivedCount >= group.groupSize) return;

            int missingIdx = -1;
            for (int i = 0; i < group.groupSize; i++) {
                if (!group.received[i]) {
                    missingIdx = i;
                    break;
                }
            }
            if (missingIdx < 0) return;

            group.recovered = true;

            // XOR 恢复
            byte[] recovered = group.parityData.clone();
            for (int i = 0; i < group.groupSize; i++) {
                if (i == missingIdx || !group.received[i]) continue;
                byte[] pkt = group.packets[i];
                for (int j = 0; j < pkt.length && j < recovered.length; j++) {
                    recovered[j] ^= pkt[j];
                }
            }

            if (recovered.length >= 2) {
                int recoveredLen = ((recovered[0] & 0xFF) << 8) | (recovered[1] & 0xFF);
                if (recoveredLen > 0 && recoveredLen <= recovered.length - 2) {
                    output.onOutput(recovered, 2, recoveredLen);
                }
            }
        }

        private FecGroup getOrCreateGroup(byte gid, byte gs) {
            for (GroupSlot slot : groups) {
                if (slot.active && slot.id == gid) {
                    return slot.group;
                }
            }
            GroupSlot slot = groups[nextSlot % MAX_GROUPS];
            slot.id = gid;
            slot.active = true;
            slot.group = new FecGroup(gs);
            nextSlot++;
            return slot.group;
        }

        public interface OutputCallback {
            void onOutput(byte[] data, int offset, int len);
        }

        private static class FecGroup {
            int groupSize;
            int receivedCount;
            boolean hasParity;
            boolean recovered;
            int parityLen;
            byte[] parityData;
            boolean[] received;
            byte[][] packets;
            int[] originalLens;

            FecGroup(int gs) {
                groupSize = gs;
                received = new boolean[gs];
                packets = new byte[gs][];
                originalLens = new int[gs];
            }
        }

        private static class GroupSlot {
            byte id;
            boolean active;
            FecGroup group;
        }
    }

    /**
     * 检查是否为 FEC 包
     */
    public static boolean isFecPacket(byte[] data, int offset, int len) {
        if (data == null || len < FEC_HEADER_SIZE) return false;
        byte type = data[offset];
        return type == FEC_TYPE_DATA || type == FEC_TYPE_PARITY;
    }
}
