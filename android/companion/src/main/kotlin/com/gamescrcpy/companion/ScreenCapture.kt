package com.gamescrcpy.companion

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.Image
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.SystemClock
import android.util.DisplayMetrics
import android.view.Display
import com.gamescrcpy.protocol.Enums.ScreenshotProblem
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

interface ProjectionHost {
    fun projecting(on: Boolean): Boolean

    fun capturePrompt(shown: Boolean)
}

class ScreenCapture(private val context: Context, private val host: ProjectionHost) : ScreenshotPort {

    private class Request(val maxSide: Int, val quality: Int, val done: (ScreenshotResult) -> Unit)

    private val main = Handler(Looper.getMainLooper())
    private val manager: MediaProjectionManager? = context.getSystemService(MediaProjectionManager::class.java)
    private val displays: DisplayManager? = context.getSystemService(DisplayManager::class.java)
    private val lock = ReentrantLock()
    private val arrived = lock.newCondition()
    private var frames: HandlerThread? = null
    private var frameHandler: Handler? = null
    private var worker: ExecutorService? = null
    private var projection: MediaProjection? = null
    private var display: VirtualDisplay? = null
    private var reader: ImageReader? = null
    private var latest: Image? = null
    private var size = ShotSize(0, 0)
    private var asking: Request? = null
    private var askedAt = 0L
    private var busy = false
    private val stopped = object : MediaProjection.Callback() {
        override fun onStop() {
            main.post { tearDown() }
        }
    }

    override fun capture(maxSide: Int, quality: Int, done: (ScreenshotResult) -> Unit) {
        main.post { begin(Request(maxSide, quality, done)) }
    }

    override fun stop() {
        main.post { tearDown() }
    }

    fun close() {
        tearDown(closing = true)
        worker?.shutdown()
        worker = null
        lock.withLock {
            frames?.quitSafely()
            frames = null
            frameHandler = null
        }
    }

    private fun begin(request: Request) {
        if (manager == null) {
            request.done(failure(ScreenshotProblem.UNSUPPORTED, "this phone cannot capture its screen"))
            return
        }
        val waiting = asking
        if (waiting != null) {
            if (SystemClock.elapsedRealtime() - askedAt < ASK_MILLIS) {
                request.done(failure(ScreenshotProblem.BUSY, "still waiting for the screen capture to be allowed"))
                return
            }
            asking = null
            waiting.done(failure(ScreenshotProblem.REFUSED, "the screen capture was not answered on the phone"))
        }
        if (busy) {
            request.done(failure(ScreenshotProblem.BUSY, "a screenshot is still being taken"))
            return
        }
        if (projection == null) {
            ask(request)
            return
        }
        shoot(request)
    }

    private fun ask(request: Request) {
        asking = request
        askedAt = SystemClock.elapsedRealtime()
        CaptureGrants.listen { grant -> main.post { granted(grant) } }
        try {
            context.startActivity(
                Intent(context, CaptureActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_NO_ANIMATION),
            )
        } catch (e: RuntimeException) {
            asking = null
            CaptureGrants.listen(null)
            request.done(failure(ScreenshotProblem.UNSUPPORTED, "the screen capture could not be asked for"))
            return
        }
        host.capturePrompt(true)
    }

    private fun granted(grant: CaptureGrant?) {
        val request = asking ?: return
        asking = null
        CaptureGrants.listen(null)
        host.capturePrompt(false)
        if (grant == null) {
            request.done(failure(ScreenshotProblem.REFUSED, "the screen capture was not allowed on the phone"))
            return
        }
        if (!host.projecting(true)) {
            request.done(failure(ScreenshotProblem.UNSUPPORTED, "the phone did not let the companion capture its screen"))
            return
        }
        val made = try {
            manager?.getMediaProjection(grant.resultCode, grant.data)
        } catch (e: RuntimeException) {
            null
        }
        if (made == null) {
            host.projecting(false)
            request.done(failure(ScreenshotProblem.UNSUPPORTED, "the phone gave no screen capture"))
            return
        }
        made.registerCallback(stopped, main)
        projection = made
        shoot(request)
    }

    private fun shoot(request: Request) {
        val projected = projection
        if (projected == null) {
            request.done(failure(ScreenshotProblem.ERROR, "the screen capture has stopped"))
            return
        }
        val metrics = screenMetrics()
        val target = ScreenshotChunks.fit(metrics.widthPixels, metrics.heightPixels, request.maxSide)
        if (target.width <= 0 || target.height <= 0) {
            request.done(failure(ScreenshotProblem.ERROR, "the size of the screen is unknown"))
            return
        }
        busy = true
        val dpi = metrics.densityDpi
        executor().execute {
            val result = grab(projected, target, dpi, request.quality)
            main.post {
                busy = false
                request.done(result)
            }
        }
    }

    private fun grab(projected: MediaProjection, target: ShotSize, dpi: Int, quality: Int): ScreenshotResult {
        val bitmap = try {
            frameAt(projected, target, dpi)
        } catch (e: RuntimeException) {
            null
        } ?: return failure(ScreenshotProblem.ERROR, "no picture came from the screen")
        try {
            for (step in ScreenshotChunks.qualities(quality)) {
                val out = ByteArrayOutputStream()
                if (!bitmap.compress(Bitmap.CompressFormat.JPEG, step, out)) {
                    break
                }
                val jpeg = out.toByteArray()
                if (ScreenshotChunks.fits(jpeg)) {
                    return ScreenshotResult.Picture(jpeg)
                }
            }
        } finally {
            bitmap.recycle()
        }
        return failure(ScreenshotProblem.ERROR, "the screenshot does not fit into 4 MiB")
    }

    private fun frameAt(projected: MediaProjection, target: ShotSize, dpi: Int): Bitmap? {
        lock.withLock {
            if (display == null || size != target) {
                val next = ImageReader.newInstance(target.width, target.height, PixelFormat.RGBA_8888, MAX_IMAGES)
                next.setOnImageAvailableListener({ source -> arrive(source) }, handlerForFrames())
                val old = reader
                latest?.close()
                latest = null
                val shown = display
                if (shown == null) {
                    display = projected.createVirtualDisplay(
                        DISPLAY_NAME,
                        target.width,
                        target.height,
                        dpi,
                        DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                        next.surface,
                        null,
                        null,
                    )
                } else {
                    shown.resize(target.width, target.height, dpi)
                    shown.surface = next.surface
                }
                reader = next
                size = target
                old?.close()
            }
            val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(FRAME_MILLIS)
            while (latest == null) {
                val left = deadline - System.nanoTime()
                if (left <= 0) {
                    return null
                }
                arrived.awaitNanos(left)
            }
            val image = latest ?: return null
            return bitmapOf(image)
        }
    }

    private fun arrive(source: ImageReader) {
        val image = try {
            source.acquireLatestImage()
        } catch (e: IllegalStateException) {
            null
        } ?: return
        lock.withLock {
            if (source !== reader) {
                image.close()
                return
            }
            latest?.close()
            latest = image
            arrived.signalAll()
        }
    }

    private fun bitmapOf(image: Image): Bitmap {
        val plane = image.planes[0]
        val width = image.width
        val height = image.height
        val pixelStride = plane.pixelStride
        val rowStride = plane.rowStride
        val rowBytes = width * BYTES_PER_PIXEL
        val packed = ByteBuffer.allocateDirect(rowBytes * height)
        val source = plane.buffer
        val row = ByteArray(rowBytes)
        for (y in 0 until height) {
            if (pixelStride == BYTES_PER_PIXEL) {
                source.position(y * rowStride)
                source.get(row, 0, rowBytes)
            } else {
                for (x in 0 until width) {
                    source.position(y * rowStride + x * pixelStride)
                    source.get(row, x * BYTES_PER_PIXEL, BYTES_PER_PIXEL)
                }
            }
            packed.put(row)
        }
        packed.rewind()
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        bitmap.copyPixelsFromBuffer(packed)
        return bitmap
    }

    private fun tearDown(closing: Boolean = false) {
        asking?.let { waiting ->
            asking = null
            waiting.done(failure(ScreenshotProblem.ERROR, "the connection ended before the screen capture was allowed"))
        }
        CaptureGrants.listen(null)
        host.capturePrompt(false)
        val ended = projection
        projection = null
        lock.withLock {
            latest?.close()
            latest = null
            display?.release()
            display = null
            reader?.close()
            reader = null
            size = ShotSize(0, 0)
        }
        if (ended != null) {
            ended.unregisterCallback(stopped)
            ended.stop()
            if (!closing) {
                host.projecting(false)
            }
        }
    }

    private fun handlerForFrames(): Handler {
        frameHandler?.let { return it }
        val thread = HandlerThread(FRAMES_THREAD)
        thread.start()
        frames = thread
        return Handler(thread.looper).also { frameHandler = it }
    }

    private fun executor(): ExecutorService = worker ?: Executors.newSingleThreadExecutor().also { worker = it }

    @Suppress("DEPRECATION")
    private fun screenMetrics(): DisplayMetrics {
        val metrics = DisplayMetrics()
        displays?.getDisplay(Display.DEFAULT_DISPLAY)?.getRealMetrics(metrics)
        return metrics
    }

    private fun failure(problem: ScreenshotProblem, text: String) = ScreenshotResult.Failure(problem, text)

    private companion object {
        const val ASK_MILLIS = 60_000L
        const val FRAME_MILLIS = 2_000L
        const val MAX_IMAGES = 3
        const val BYTES_PER_PIXEL = 4
        const val DISPLAY_NAME = "GameScrcpy screenshot"
        const val FRAMES_THREAD = "companion-frames"
    }
}
