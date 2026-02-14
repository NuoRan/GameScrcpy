/**
 * @file FecCodec.h
 * @brief FEC 前向纠错编解码器 / Forward Error Correction Codec
 *
 * [超低延迟优化] 基于 XOR 的简单 FEC，用于 KCP 传输层。
 * Simple XOR-based FEC for KCP transport layer.
 *
 * 原理：每 N 个数据包生成 1 个 FEC 校验包。
 * 当丢失 1 个数据包时，可通过其余 N-1 个数据包和 FEC 包恢复，
 * 无需等待 KCP 重传（至少 1 RTT），消除尾延迟。
 *
 * Principle: For every N data packets, generate 1 FEC parity packet.
 * When 1 packet is lost, recover from remaining N-1 packets + FEC packet,
 * eliminating the need to wait for KCP retransmission (at least 1 RTT).
 *
 * 编码格式 / Encoding format:
 *   [1B type] [1B groupId] [1B index] [1B groupSize] [2B originalLen] [payload...]
 *   type: 0x01 = 数据包, 0x02 = FEC 校验包
 *
 * 默认配置: groupSize=10, fecCount=2 (10:2 编码)
 * 最多容忍同组内丢失 1 个包（XOR FEC 限制，非 Reed-Solomon）
 *
 * 线程安全: 编码器和解码器各自线程安全，可在不同线程使用。
 */

#ifndef FEC_CODEC_H
#define FEC_CODEC_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <functional>
#include <mutex>
#include <array>
#include <algorithm>

namespace fec {

// FEC 包头大小
static constexpr int FEC_HEADER_SIZE = 6;

// 包类型
static constexpr uint8_t FEC_TYPE_DATA = 0x01;
static constexpr uint8_t FEC_TYPE_PARITY = 0x02;

/**
 * @brief FEC 编码器 / FEC Encoder
 *
 * 将原始数据包编码为带 FEC 头的数据包 + 周期性的校验包。
 * 用于发送端，每 groupSize 个原始包生成 1 个 XOR 校验包。
 */
class FecEncoder {
public:
    /**
     * @param groupSize 每组数据包数量 (默认 10)
     * @param maxPacketSize 单包最大大小 (默认 1400，匹配 KCP MTU)
     */
    explicit FecEncoder(int groupSize = 10, int maxPacketSize = 1400)
        : m_groupSize(groupSize)
        , m_maxPacketSize(maxPacketSize)
        , m_parityBuf(maxPacketSize, 0)
    {
        reset();
    }

    /**
     * @brief 编码一个数据包
     *
     * @param data 原始数据
     * @param len 数据长度
     * @param outputCb 输出回调: (data, len) 每个输出包调用一次
     *                 可能被调用 1 次（数据包）或 2 次（数据包 + FEC 校验包）
     */
    void encode(const uint8_t* data, int len,
                std::function<void(const uint8_t*, int)> outputCb)
    {
        if (!data || len <= 0 || len > m_maxPacketSize - FEC_HEADER_SIZE) {
            // 超过 MTU 的包不做 FEC，直接透传
            if (outputCb && data && len > 0) {
                outputCb(data, len);
            }
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        // 构建 FEC 数据包: [type=0x01][groupId][index][groupSize][originalLen(2B)][payload]
        m_encodeBuf.resize(FEC_HEADER_SIZE + len);
        m_encodeBuf[0] = FEC_TYPE_DATA;
        m_encodeBuf[1] = m_groupId;
        m_encodeBuf[2] = static_cast<uint8_t>(m_index);
        m_encodeBuf[3] = static_cast<uint8_t>(m_groupSize);
        m_encodeBuf[4] = static_cast<uint8_t>((len >> 8) & 0xFF);
        m_encodeBuf[5] = static_cast<uint8_t>(len & 0xFF);
        memcpy(m_encodeBuf.data() + FEC_HEADER_SIZE, data, len);

        // 输出数据包
        if (outputCb) {
            outputCb(m_encodeBuf.data(), static_cast<int>(m_encodeBuf.size()));
        }

        // 更新 XOR 校验: parityBuf ^= paddedData
        int paddedLen = len + 2; // 包含 originalLen 字段
        if (paddedLen > static_cast<int>(m_parityBuf.size())) {
            m_parityBuf.resize(paddedLen, 0);
        }

        // XOR originalLen
        m_parityBuf[0] ^= m_encodeBuf[4];
        m_parityBuf[1] ^= m_encodeBuf[5];
        // XOR payload
        for (int i = 0; i < len; ++i) {
            m_parityBuf[2 + i] ^= data[i];
        }
        if (paddedLen > m_maxParityLen) {
            m_maxParityLen = paddedLen;
        }

        m_index++;

        // 一组完成，发射 FEC 校验包
        if (m_index >= m_groupSize) {
            // 构建 FEC 校验包: [type=0x02][groupId][index=groupSize][groupSize][parityData...]
            m_encodeBuf.resize(FEC_HEADER_SIZE + m_maxParityLen);
            m_encodeBuf[0] = FEC_TYPE_PARITY;
            m_encodeBuf[1] = m_groupId;
            m_encodeBuf[2] = static_cast<uint8_t>(m_groupSize); // FEC index = groupSize
            m_encodeBuf[3] = static_cast<uint8_t>(m_groupSize);
            m_encodeBuf[4] = static_cast<uint8_t>((m_maxParityLen >> 8) & 0xFF);
            m_encodeBuf[5] = static_cast<uint8_t>(m_maxParityLen & 0xFF);
            memcpy(m_encodeBuf.data() + FEC_HEADER_SIZE, m_parityBuf.data(), m_maxParityLen);

            if (outputCb) {
                outputCb(m_encodeBuf.data(), static_cast<int>(m_encodeBuf.size()));
            }

            // 重置组
            m_groupId++;
            reset();
        }
    }

private:
    void reset() {
        m_index = 0;
        m_maxParityLen = 0;
        std::fill(m_parityBuf.begin(), m_parityBuf.end(), 0);
    }

    int m_groupSize;
    int m_maxPacketSize;
    uint8_t m_groupId = 0;
    int m_index = 0;
    int m_maxParityLen = 0;
    std::vector<uint8_t> m_parityBuf;
    std::vector<uint8_t> m_encodeBuf;
    std::mutex m_mutex;
};

/**
 * @brief FEC 解码器 / FEC Decoder
 *
 * 接收带 FEC 头的数据包，尝试恢复丢失的包。
 * 用于接收端。
 */
class FecDecoder {
public:
    explicit FecDecoder(int maxGroupSize = 16, int maxPacketSize = 1400)
        : m_maxGroupSize(maxGroupSize)
        , m_maxPacketSize(maxPacketSize)
    {
    }

    /**
     * @brief 解码一个 FEC 包
     *
     * @param data FEC 编码的数据
     * @param len 数据长度
     * @param outputCb 输出回调: (data, len) 每个恢复的原始数据包调用一次
     */
    void decode(const uint8_t* data, int len,
                std::function<void(const uint8_t*, int)> outputCb)
    {
        if (!data || len < FEC_HEADER_SIZE || !outputCb) {
            // 非 FEC 包，直接透传
            if (outputCb && data && len > 0) {
                outputCb(data, len);
            }
            return;
        }

        uint8_t type = data[0];
        uint8_t groupId = data[1];
        uint8_t index = data[2];
        uint8_t groupSize = data[3];

        // 验证基本字段
        if (type != FEC_TYPE_DATA && type != FEC_TYPE_PARITY) {
            // 非 FEC 包，直接透传
            outputCb(data, len);
            return;
        }

        if (groupSize == 0 || groupSize > m_maxGroupSize) {
            return; // 无效组大小
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        // 查找或创建组
        FecGroup& group = getOrCreateGroup(groupId, groupSize);

        int payloadLen = len - FEC_HEADER_SIZE;

        if (type == FEC_TYPE_DATA) {
            // 数据包：提取原始数据并输出
            int originalLen = (data[4] << 8) | data[5];
            if (originalLen <= 0 || originalLen > payloadLen) {
                return; // 无效长度
            }

            // 记录已收到
            if (index < groupSize && !group.received[index]) {
                group.received[index] = true;
                group.receivedCount++;

                // 保存数据用于可能的恢复
                group.packets[index].assign(data + FEC_HEADER_SIZE, data + len);
                group.originalLens[index] = originalLen;
            }

            // 输出原始数据
            outputCb(data + FEC_HEADER_SIZE + 2, originalLen);

        } else if (type == FEC_TYPE_PARITY) {
            // FEC 校验包
            int parityLen = (data[4] << 8) | data[5];
            if (parityLen <= 0 || parityLen > payloadLen) {
                return;
            }

            group.hasParity = true;
            group.parityData.assign(data + FEC_HEADER_SIZE, data + FEC_HEADER_SIZE + parityLen);
            group.parityLen = parityLen;

            // 尝试恢复丢失的包
            tryRecover(group, outputCb);
        }

        // 尝试恢复（数据包到达后也可能触发）
        if (group.hasParity && group.receivedCount == groupSize - 1) {
            tryRecover(group, outputCb);
        }
    }

    /**
     * @brief 检查输入是否为 FEC 编码的包
     */
    static bool isFecPacket(const uint8_t* data, int len) {
        if (!data || len < FEC_HEADER_SIZE) return false;
        return data[0] == FEC_TYPE_DATA || data[0] == FEC_TYPE_PARITY;
    }

private:
    struct FecGroup {
        uint8_t groupSize = 0;
        int receivedCount = 0;
        bool hasParity = false;
        bool recovered = false;  // 是否已经尝试过恢复
        int parityLen = 0;
        std::vector<uint8_t> parityData;
        std::vector<bool> received;
        std::vector<std::vector<uint8_t>> packets;
        std::vector<int> originalLens;

        void init(uint8_t gs) {
            groupSize = gs;
            receivedCount = 0;
            hasParity = false;
            recovered = false;
            parityLen = 0;
            parityData.clear();
            received.assign(gs, false);
            packets.resize(gs);
            originalLens.assign(gs, 0);
        }
    };

    FecGroup& getOrCreateGroup(uint8_t groupId, uint8_t groupSize) {
        // 使用环形缓冲区管理组（最多缓存 4 个组）
        for (auto& g : m_groups) {
            if (g.id == groupId && g.active) {
                return g.group;
            }
        }
        // 创建新组（覆盖最旧的）
        auto& slot = m_groups[m_nextSlot % MAX_GROUPS];
        slot.id = groupId;
        slot.active = true;
        slot.group.init(groupSize);
        m_nextSlot++;
        return slot.group;
    }

    void tryRecover(FecGroup& group, std::function<void(const uint8_t*, int)>& outputCb) {
        if (group.recovered || !group.hasParity) return;
        if (group.receivedCount < group.groupSize - 1) return;
        if (group.receivedCount >= group.groupSize) return; // 全收到了，无需恢复

        // 找到丢失的包索引
        int missingIdx = -1;
        for (int i = 0; i < group.groupSize; ++i) {
            if (!group.received[i]) {
                missingIdx = i;
                break;
            }
        }
        if (missingIdx < 0) return;

        group.recovered = true;

        // XOR 恢复：recovered = parity ^ all_other_packets
        std::vector<uint8_t> recovered(group.parityData);

        for (int i = 0; i < group.groupSize; ++i) {
            if (i == missingIdx) continue;
            if (!group.received[i]) continue;

            const auto& pkt = group.packets[i];
            for (size_t j = 0; j < pkt.size() && j < recovered.size(); ++j) {
                recovered[j] ^= pkt[j];
            }
        }

        // 提取恢复的原始数据
        if (recovered.size() >= 2) {
            int recoveredLen = (recovered[0] << 8) | recovered[1];
            if (recoveredLen > 0 && recoveredLen <= static_cast<int>(recovered.size()) - 2) {
                outputCb(recovered.data() + 2, recoveredLen);
            }
        }
    }

    static constexpr int MAX_GROUPS = 4;
    struct GroupSlot {
        uint8_t id = 0;
        bool active = false;
        FecGroup group;
    };
    std::array<GroupSlot, MAX_GROUPS> m_groups;
    int m_nextSlot = 0;

    int m_maxGroupSize;
    int m_maxPacketSize;
    std::mutex m_mutex;
};

} // namespace fec

#endif // FEC_CODEC_H
