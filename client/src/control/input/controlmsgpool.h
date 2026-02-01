#ifndef CONTROLMSGPOOL_H
#define CONTROLMSGPOOL_H

#include <QMutex>
#include <QStack>
#include <memory>
#include "controlmsg.h"

/**
 * @brief ControlMsg 对象池
 *
 * 线程安全的对象池实现，用于减少 ControlMsg 的堆分配开销。
 *
 * 使用方式：
 *   auto msg = ControlMsgPool::instance().acquire(ControlMsg::CMT_INJECT_TOUCH);
 *   msg->setInjectTouchMsgData(...);
 *   sendControlMsg(msg.release());  // 转移所有权给发送队列
 *
 * 或者使用 RAII：
 *   {
 *       auto msg = ControlMsgPool::instance().acquire(ControlMsg::CMT_INJECT_KEYCODE);
 *       msg->setInjectKeycodeMsgData(...);
 *       sendControlMsg(msg.release());
 *   }
 */
class ControlMsgPool
{
public:
    // 自定义删除器，支持对象回收到池中
    struct PoolDeleter {
        bool m_returnToPool;

        PoolDeleter(bool returnToPool = true) : m_returnToPool(returnToPool) {}

        void operator()(ControlMsg* msg) const {
            if (msg) {
                if (m_returnToPool) {
                    ControlMsgPool::instance().release(msg);
                } else {
                    delete msg;
                }
            }
        }
    };

    using PooledMsgPtr = std::unique_ptr<ControlMsg, PoolDeleter>;

    static ControlMsgPool& instance() {
        static ControlMsgPool pool;
        return pool;
    }

    /**
     * @brief 从池中获取一个 ControlMsg 对象
     * @param type 消息类型
     * @return 智能指针管理的 ControlMsg
     */
    PooledMsgPtr acquire(ControlMsg::ControlMsgType type) {
        ControlMsg* msg = nullptr;

        {
            QMutexLocker locker(&m_mutex);
            if (!m_pool.isEmpty()) {
                msg = m_pool.pop();
            }
        }

        if (!msg) {
            msg = new ControlMsg(type);
        } else {
            // 重置消息类型
            msg->resetType(type);
        }

        // 注意：这里使用 returnToPool=false，因为通常消息会通过 release() 转移所有权
        // 实际回收由 ControlMsgPool::release() 处理
        return PooledMsgPtr(msg, PoolDeleter(false));
    }

    /**
     * @brief 释放一个裸指针（转移所有权给发送队列后，由接收方调用）
     * @param msg 要释放的消息
     *
     * 调用方式：在 Controller 处理完消息后调用
     */
    void release(ControlMsg* msg) {
        if (!msg) return;

        QMutexLocker locker(&m_mutex);

        // 池满时直接删除
        if (m_pool.size() >= MAX_POOL_SIZE) {
            delete msg;
            return;
        }

        // 清理消息状态后放回池中
        msg->cleanup();
        m_pool.push(msg);
    }

    /**
     * @brief 获取池中当前对象数量（用于调试）
     */
    int poolSize() const {
        QMutexLocker locker(&m_mutex);
        return m_pool.size();
    }

    /**
     * @brief 预分配对象到池中
     * @param count 预分配数量
     */
    void preallocate(int count) {
        QMutexLocker locker(&m_mutex);
        for (int i = 0; i < count && m_pool.size() < MAX_POOL_SIZE; ++i) {
            m_pool.push(new ControlMsg(ControlMsg::CMT_NULL));
        }
    }

private:
    ControlMsgPool() {
        // 预分配一些对象
        preallocate(INITIAL_POOL_SIZE);
    }

    ~ControlMsgPool() {
        QMutexLocker locker(&m_mutex);
        while (!m_pool.isEmpty()) {
            delete m_pool.pop();
        }
    }

    // 禁止拷贝
    ControlMsgPool(const ControlMsgPool&) = delete;
    ControlMsgPool& operator=(const ControlMsgPool&) = delete;

private:
    static constexpr int MAX_POOL_SIZE = 64;      // 最大池大小
    static constexpr int INITIAL_POOL_SIZE = 16;  // 初始预分配数量

    mutable QMutex m_mutex;
    QStack<ControlMsg*> m_pool;
};

#endif // CONTROLMSGPOOL_H
