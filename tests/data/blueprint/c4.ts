import { keys, every, vision, touch } from 'gamescrcpy'
// controls（组件）、vars（变量）、res（素材）按这个方案自动生成，不用导入

// F：单击拾取，长按开背包
keys.F.onTap(() => controls.拾取.tap())
keys.F.onLongPress(500, () => controls.背包.tap())

// 开镜时左键连发
keys.MouseLeft.onPress(() => {
  if (controls.开镜.holding) controls.连发.start()
  else controls.开火.press()
})
keys.MouseLeft.onRelease(() => {
  controls.连发.stop()
  controls.开火.release()
})

// 看到“拾取”就点
every(500, async () => {
  const 位置 = await vision.find(res.拾取)
  if (位置) await touch.tap(位置)
})
