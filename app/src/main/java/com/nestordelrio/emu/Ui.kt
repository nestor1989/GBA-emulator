package com.nestordelrio.emu

import android.app.Activity
import android.content.Context
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController

fun Context.dp(value: Int): Int = (value * resources.displayMetrics.density + 0.5f).toInt()

fun rounded(color: Int, radius: Float, strokeColor: Int? = null, strokeWidth: Int = 0): GradientDrawable =
    GradientDrawable().apply {
        setColor(color)
        cornerRadius = radius
        if (strokeColor != null) setStroke(strokeWidth, strokeColor)
    }

/**
 * Pads [view] so nothing sits under the system bars (optional) or a camera
 * cutout. Needed because apps targeting Android 15+ are always edge-to-edge.
 */
fun applySafeInsets(view: View, includeSystemBars: Boolean) {
    val baseLeft = view.paddingLeft
    val baseTop = view.paddingTop
    val baseRight = view.paddingRight
    val baseBottom = view.paddingBottom
    view.setOnApplyWindowInsetsListener { v, insets ->
        val safe = safeInsets(insets, includeSystemBars)
        v.setPadding(baseLeft + safe[0], baseTop + safe[1], baseRight + safe[2], baseBottom + safe[3])
        insets
    }
    view.requestApplyInsets()
}

/** left, top, right, bottom */
private fun safeInsets(insets: WindowInsets, includeSystemBars: Boolean): IntArray {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        var types = WindowInsets.Type.displayCutout()
        if (includeSystemBars) types = types or WindowInsets.Type.systemBars()
        val safe = insets.getInsets(types)
        return intArrayOf(safe.left, safe.top, safe.right, safe.bottom)
    }
    return legacySafeInsets(insets, includeSystemBars)
}

@Suppress("DEPRECATION")
private fun legacySafeInsets(insets: WindowInsets, includeSystemBars: Boolean): IntArray {
    val result = IntArray(4)
    if (includeSystemBars) {
        result[0] = insets.systemWindowInsetLeft
        result[1] = insets.systemWindowInsetTop
        result[2] = insets.systemWindowInsetRight
        result[3] = insets.systemWindowInsetBottom
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
        val cutout = insets.displayCutout
        if (cutout != null) {
            result[0] = maxOf(result[0], cutout.safeInsetLeft)
            result[1] = maxOf(result[1], cutout.safeInsetTop)
            result[2] = maxOf(result[2], cutout.safeInsetRight)
            result[3] = maxOf(result[3], cutout.safeInsetBottom)
        }
    }
    return result
}

/** Hides status and navigation bars; a swipe from the edge shows them briefly. */
fun Activity.enterImmersiveMode() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        window.setDecorFitsSystemWindows(false)
        window.insetsController?.let { controller ->
            controller.hide(WindowInsets.Type.systemBars())
            controller.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    } else {
        legacyImmersive(this)
    }
}

@Suppress("DEPRECATION")
private fun legacyImmersive(activity: Activity) {
    activity.window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
        View.SYSTEM_UI_FLAG_FULLSCREEN or
        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
        View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
}
