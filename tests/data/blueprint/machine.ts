import { vision, machine } from 'gamescrcpy'

export async function 刷副本() {
  await machine({
    // 点“开始”，最多等 10 秒
    async 大厅(to) {
      await vision.findAndTap(res.开始)
      return to.匹配中("点了开始")
    },
    async 匹配中(to) {
      const ok = await vision.waitFor(res.对局中, { timeoutMs: 120000 })
      if (!ok) return to.大厅("超时 120 秒 · 重试")
      return to.对局中("场景 = 对局中")
    },
    async 对局中(to) {
      await sleep(1000)
      return to.结算()
    },
    async 结算(to) {
      return
    },
  }, { name: "挂机" })
}
