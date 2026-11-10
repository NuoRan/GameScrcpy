package com.gamescrcpy.companion

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class CursorTest {

    private fun sample(x: Int, y: Int = x, pressed: Boolean = false) = CursorSample(x, y, visible = true, pressed = pressed)

    @Test
    fun theLatestSampleWinsAndOneFrameIsRequestedAtATime() {
        var requests = 0
        val coalescer = CursorCoalescer { ++requests }
        assertNull(coalescer.take())

        coalescer.offer(sample(1))
        coalescer.offer(sample(2))
        coalescer.offer(sample(3, pressed = true))
        assertEquals(1, requests)
        assertEquals(sample(3, pressed = true), coalescer.take())

        coalescer.offer(CursorSample.HIDDEN)
        assertEquals(2, requests)
        assertEquals(CursorSample.HIDDEN, coalescer.take())
        assertEquals(CursorSample.HIDDEN, coalescer.take())
        assertEquals(2, requests)
    }

    @Test
    fun aSampleFromTheReadingThreadIsNeverLostBetweenFrames() {
        val requests = AtomicInteger()
        val coalescer = CursorCoalescer { requests.incrementAndGet() }
        val done = CountDownLatch(1)
        val reader = thread {
            for (i in 1..20_000) {
                coalescer.offer(sample(i % 65536))
            }
            done.countDown()
        }
        while (!done.await(0, TimeUnit.MILLISECONDS)) {
            coalescer.take()
        }
        reader.join()
        assertEquals(sample(20_000 % 65536), coalescer.take())
        assertTrue(requests.get() >= 1)
    }

    @Test
    fun portraitAndLandscapeSpreadTheFullScaleOverTheWholeOverlay() {
        val portrait = OverlayGeometry(1080, 2400, 0, 0, 1080, 2400)
        assertEquals(0f, CursorMapper.x(sample(0), portrait))
        assertEquals(0f, CursorMapper.y(sample(0), portrait))
        assertEquals(1079f, CursorMapper.x(sample(65535), portrait))
        assertEquals(2399f, CursorMapper.y(sample(65535), portrait))
        assertEquals(540f, CursorMapper.x(sample(32768), portrait), 0.05f)
        assertEquals(1200f, CursorMapper.y(sample(32768), portrait), 0.05f)

        val landscape = OverlayGeometry(2400, 1080, 0, 0, 2400, 1080)
        assertEquals(1200f, CursorMapper.x(sample(32768), landscape), 0.05f)
        assertEquals(540f, CursorMapper.y(sample(32768), landscape), 0.05f)
        assertEquals(2399f, CursorMapper.x(sample(65535), landscape))
        assertEquals(1079f, CursorMapper.y(sample(65535), landscape))
    }

    @Test
    fun anOverlayKeptOutOfTheCutoutOrTheStatusBarIsOffsetByItsOrigin() {
        val besideCutout = OverlayGeometry(2400, 1080, 100, 0, 2300, 1080)
        assertEquals(0f, CursorMapper.x(sample(0), besideCutout))
        assertEquals(1100f, CursorMapper.x(sample(32768), besideCutout), 0.05f)
        assertEquals(2299f, CursorMapper.x(sample(65535), besideCutout))
        assertEquals(540f, CursorMapper.y(sample(32768), besideCutout), 0.05f)

        val belowStatusBar = OverlayGeometry(1080, 2400, 0, 80, 1080, 2320)
        assertEquals(1120f, CursorMapper.y(sample(32768), belowStatusBar), 0.05f)
        assertEquals(0f, CursorMapper.y(sample(1000), belowStatusBar))
        assertEquals(2319f, CursorMapper.y(sample(65535), belowStatusBar))
    }

    @Test
    fun nothingIsPlacedWithoutASize() {
        assertEquals(0f, CursorMapper.map(32768, 0))
        assertEquals(0f, CursorMapper.place(32768, 1080, 0, 0))
        assertEquals(0f, CursorMapper.place(32768, 0, 0, 1080))
    }
}
