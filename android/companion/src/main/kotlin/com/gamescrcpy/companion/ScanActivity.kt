package com.gamescrcpy.companion

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import java.util.concurrent.ExecutionException
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

class ScanActivity : Activity(), LifecycleOwner {
    private val registry = LifecycleRegistry(this)
    private val decoder = QrDecoder()
    private lateinit var preview: PreviewView
    private lateinit var status: TextView
    private lateinit var settings: Button
    private var analysis: ExecutorService? = null

    @Volatile
    private var finished = false

    override val lifecycle: Lifecycle
        get() = registry

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        registry.currentState = Lifecycle.State.CREATED
        val padding = dp(PADDING_DP)
        preview = PreviewView(this).apply { scaleType = PreviewView.ScaleType.FILL_CENTER }
        status = TextView(this).apply {
            setText(R.string.scan_title)
            setTextColor(KeyHintLook.TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, BODY_SP)
            gravity = Gravity.CENTER
        }
        settings = Button(this).apply {
            setText(R.string.overlay_open)
            visibility = View.GONE
            setOnClickListener {
                startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName")))
            }
        }
        val cancel = Button(this).apply {
            setText(R.string.scan_cancel)
            setOnClickListener { finish() }
        }
        val panel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(PANEL_COLOR)
            setPadding(padding, padding, padding, padding)
            addView(status)
            addView(settings)
            addView(cancel)
        }
        val root = FrameLayout(this)
        root.addView(preview, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        root.addView(
            panel,
            FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM),
        )
        panel.setOnApplyWindowInsetsListener { view, insets ->
            view.setPadding(padding, padding, padding, padding + bottomInset(insets))
            insets
        }
        setContentView(root)
        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            startCamera()
        } else {
            requestPermissions(arrayOf(Manifest.permission.CAMERA), REQUEST_CAMERA)
        }
    }

    override fun onStart() {
        super.onStart()
        registry.currentState = Lifecycle.State.STARTED
    }

    override fun onResume() {
        super.onResume()
        registry.currentState = Lifecycle.State.RESUMED
        if (settings.visibility == View.VISIBLE &&
            checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED
        ) {
            settings.visibility = View.GONE
            status.setText(R.string.scan_title)
            startCamera()
        }
    }

    override fun onPause() {
        registry.currentState = Lifecycle.State.STARTED
        super.onPause()
    }

    override fun onStop() {
        registry.currentState = Lifecycle.State.CREATED
        super.onStop()
    }

    override fun onDestroy() {
        registry.currentState = Lifecycle.State.DESTROYED
        analysis?.shutdown()
        analysis = null
        super.onDestroy()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQUEST_CAMERA) {
            return
        }
        if (grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) {
            startCamera()
        } else {
            status.setText(R.string.scan_camera_needed)
            settings.visibility = View.VISIBLE
        }
    }

    private fun startCamera() {
        val future = ProcessCameraProvider.getInstance(this)
        future.addListener({
            val provider = try {
                future.get()
            } catch (e: ExecutionException) {
                status.setText(R.string.scan_camera_needed)
                return@addListener
            } catch (e: InterruptedException) {
                Thread.currentThread().interrupt()
                return@addListener
            }
            bind(provider)
        }, mainExecutor)
    }

    private fun bind(provider: ProcessCameraProvider) {
        if (isDestroyed) {
            return
        }
        val executor = analysis ?: Executors.newSingleThreadExecutor().also { analysis = it }
        val shown = Preview.Builder().build()
        shown.setSurfaceProvider(preview.surfaceProvider)
        val reading = ImageAnalysis.Builder()
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .build()
        reading.setAnalyzer(executor) { image -> analyze(image) }
        try {
            provider.unbindAll()
            provider.bindToLifecycle(this, CameraSelector.DEFAULT_BACK_CAMERA, shown, reading)
        } catch (e: IllegalArgumentException) {
            status.setText(R.string.scan_camera_needed)
        } catch (e: IllegalStateException) {
            status.setText(R.string.scan_camera_needed)
        }
    }

    private fun analyze(image: ImageProxy) {
        try {
            if (finished) {
                return
            }
            val plane = image.planes.firstOrNull() ?: return
            val text = decoder.decode(plane.buffer, plane.rowStride, image.width, image.height) ?: return
            runOnUiThread { found(text) }
        } finally {
            image.close()
        }
    }

    private fun found(text: String) {
        if (finished) {
            return
        }
        if (QrLinks.parse(text) == null) {
            status.setText(R.string.scan_not_ours)
            return
        }
        finished = true
        startForegroundService(
            Intent(this, CompanionService::class.java)
                .setAction(CompanionService.ACTION_PAIR)
                .putExtra(CompanionService.EXTRA_LINK, text),
        )
        finish()
    }

    private fun bottomInset(insets: WindowInsets): Int {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return insets.getInsets(WindowInsets.Type.systemBars()).bottom
        }
        return legacyBottom(insets)
    }

    @Suppress("DEPRECATION")
    private fun legacyBottom(insets: WindowInsets): Int = insets.systemWindowInsetBottom

    private fun dp(value: Float): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, resources.displayMetrics).toInt()

    private companion object {
        const val REQUEST_CAMERA = 1
        const val PADDING_DP = 20f
        const val BODY_SP = 16f
        const val PANEL_COLOR = 0xCC0B0C0F.toInt()
    }
}
