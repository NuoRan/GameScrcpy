package com.gamescrcpy.companion

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
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
    private lateinit var settings: TextView
    private var analysis: ExecutorService? = null

    @Volatile
    private var finished = false

    override val lifecycle: Lifecycle
        get() = registry

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        registry.currentState = Lifecycle.State.CREATED
        val ui = Ui(this, Look.palette(this, null))
        val padding = ui.px(Look.SPACE_XL)
        val margin = ui.px(Look.SPACE_MD)
        preview = PreviewView(this).apply { scaleType = PreviewView.ScaleType.FILL_CENTER }
        status = ui.text(Look.BODY, ui.palette.textPrimary, R.string.scan_title).apply {
            gravity = Gravity.CENTER
        }
        settings = ui.button(ButtonKind.SECONDARY, R.string.scan_open_settings).apply {
            visibility = View.GONE
            setOnClickListener {
                startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName")))
            }
        }
        val cancel = ui.button(ButtonKind.GHOST, R.string.action_cancel).apply {
            setOnClickListener { finish() }
        }
        val wide = { LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT) }
        val panel = ui.column(Look.SPACE_MD).apply {
            background = ui.rounded(Look.alpha(ui.palette.bgElevated, PANEL_ALPHA), Look.RADIUS_XL, ui.palette.strokeStrong)
            setPadding(padding, padding, padding, ui.px(Look.SPACE_MD))
            addView(status, wide())
            addView(settings, wide())
            addView(cancel, wide())
        }
        val root = FrameLayout(this)
        root.setBackgroundColor(ui.palette.bgBase)
        root.addView(preview, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        val placed = FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM)
        placed.setMargins(margin, margin, margin, margin)
        root.addView(panel, placed)
        panel.setOnApplyWindowInsetsListener { view, insets ->
            val layout = view.layoutParams as FrameLayout.LayoutParams
            layout.bottomMargin = margin + bottomInset(insets)
            view.layoutParams = layout
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

    private companion object {
        const val REQUEST_CAMERA = 1
        const val PANEL_ALPHA = 0.92f
    }
}
