package com.nestordelrio.emu

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Typeface
import android.os.Build
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import com.nestordelrio.emu.core.Keys
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/**
 * On-screen controls drawn and hit-tested in a single multitouch view (not one
 * View per button), so a thumb can slide from B to A or roll around the cross
 * without lifting.
 *
 * - A finger that lands on the cross keeps steering it while it moves, even
 *   outside the drawn cross.
 * - Any other finger presses whatever button is under it, re-checked on every
 *   move. The space between A and B presses both.
 */
class GamepadView(context: Context) : View(context) {

    private companion object {
        // Portrait layout knobs: share of the panel's free height left above
        // each part (0 = right under the quick bar, 1 = at the bottom).
        const val PORTRAIT_MAIN_BIAS = 0.2f   // shoulders, cross and A/B
        const val PORTRAIT_PILLS_BIAS = 0.55f // SELECT/START (unchanged)
    }

    /** Receives the full button mask on every change. */
    var onKeysChanged: ((Int) -> Unit)? = null

    /**
     * true: controls float over the game (landscape). false: the view is a
     * panel below the game (portrait).
     */
    var overlay: Boolean = false
        set(value) {
            if (field != value) {
                field = value
                layoutControls(width, height)
                invalidate()
            }
        }

    /** Colors; see [ControlTheme]. */
    var theme: ControlTheme = ControlThemes.CLASSIC
        set(value) {
            if (field !== value) {
                field = value
                invalidate()
            }
        }

    private class Button(val key: Int, val label: String, val round: Boolean) {
        val rect = RectF()
        val hit = RectF()
    }

    private val density = resources.displayMetrics.density
    private fun dp(value: Float) = value * density

    private val buttonA = Button(Keys.A, "A", round = true)
    private val buttonB = Button(Keys.B, "B", round = true)
    private val buttonL = Button(Keys.L, "L", round = false)
    private val buttonR = Button(Keys.R, "R", round = false)
    private val buttonSelect = Button(Keys.SELECT, "SELECT", round = false)
    private val buttonStart = Button(Keys.START, "START", round = false)
    private val buttons = listOf(buttonA, buttonB, buttonL, buttonR, buttonSelect, buttonStart)

    private var dpadX = 0f
    private var dpadY = 0f
    private var dpadRadius = 1f

    private val dpadPointers = HashSet<Int>()
    private var mask = 0

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.5f)
    }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        layoutControls(w, h)
    }

    private fun layoutControls(w: Int, h: Int) {
        if (w <= 0 || h <= 0) return
        val width = w.toFloat()
        val height = h.toFloat()
        // Sizes are designed for a 360 dp wide panel and scaled to fit; the
        // portrait panel grows the controls when the phone is wide enough.
        val scale = if (overlay) {
            (height / dp(300f)).coerceIn(0.8f, 1.3f)
        } else {
            min(width / dp(360f), height / dp(290f)).coerceIn(0.75f, 1.5f)
        }
        fun s(value: Float) = dp(value) * scale

        val margin = s(16f)
        val shoulderW = s(110f)
        val shoulderH = s(44f)
        val faceRadius = s(34f)
        val pillW = s(76f)
        val pillH = s(30f)
        val gap = s(20f)
        dpadRadius = s(72f)

        val top: Float
        val clusterY: Float
        val pillY: Float
        if (overlay) {
            // Landscape: shoulders in the top corners, the rest at the bottom.
            top = margin
            pillY = height - margin - pillH / 2f
            clusterY = pillY - pillH / 2f - s(70f)
        } else {
            // Portrait: the free height of the panel (`spare`) is shared out.
            // SELECT/START sit low, near the resting thumbs; shoulders, cross
            // and A/B move up toward the quick bar so they don't crowd
            // SELECT/START and no empty band is left under the bar.
            val groupHeight = shoulderH + gap + dpadRadius * 2f + gap + pillH
            val spare = max(0f, height - groupHeight - margin * 2f)
            top = margin + spare * PORTRAIT_MAIN_BIAS
            clusterY = top + shoulderH + gap + dpadRadius
            val pillsTop = margin + spare * PORTRAIT_PILLS_BIAS + shoulderH + gap + dpadRadius * 2f + gap
            pillY = pillsTop + pillH / 2f
        }

        buttonL.rect.set(margin, top, margin + shoulderW, top + shoulderH)
        buttonR.rect.set(width - margin - shoulderW, top, width - margin, top + shoulderH)
        buttonSelect.rect.set(width / 2f - s(8f) - pillW, pillY - pillH / 2f, width / 2f - s(8f), pillY + pillH / 2f)
        buttonStart.rect.set(width / 2f + s(8f), pillY - pillH / 2f, width / 2f + s(8f) + pillW, pillY + pillH / 2f)

        dpadX = margin + dpadRadius + s(4f)
        dpadY = clusterY

        val aX = width - margin - faceRadius - s(6f)
        val aY = clusterY - s(22f)
        buttonA.rect.set(aX - faceRadius, aY - faceRadius, aX + faceRadius, aY + faceRadius)
        val bX = aX - s(80f)
        val bY = clusterY + s(24f)
        buttonB.rect.set(bX - faceRadius, bY - faceRadius, bX + faceRadius, bY + faceRadius)

        // Generous touch areas; the A/B areas overlap in the middle on purpose.
        val slack = s(14f)
        for (button in buttons) {
            button.hit.set(button.rect)
            button.hit.inset(-slack, -slack)
        }
        text.textSize = s(15f)
        buildCross()
        excludeSystemGestures(slack)
    }

    /**
     * Keeps edge swipes on the cross and on A/B from triggering the system
     * back gesture (Android 10+). Android caps this at 200 dp per edge.
     */
    private fun excludeSystemGestures(slack: Float) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        val reach = dpadRadius * 1.25f
        val dpadArea = Rect(
            (dpadX - reach).toInt(), (dpadY - reach).toInt(),
            (dpadX + reach).toInt(), (dpadY + reach).toInt(),
        )
        val faceArea = Rect(
            (buttonB.rect.left - slack).toInt(), (buttonA.rect.top - slack).toInt(),
            width, (buttonB.rect.bottom + slack).toInt(),
        )
        systemGestureExclusionRects = listOf(dpadArea, faceArea)
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val action = event.actionMasked
        var lifted = -1
        when (action) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                if (action == MotionEvent.ACTION_DOWN) dpadPointers.clear()
                val index = event.actionIndex
                val distance = hypot(event.getX(index) - dpadX, event.getY(index) - dpadY)
                if (distance <= dpadRadius * 1.25f) dpadPointers.add(event.getPointerId(index))
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                lifted = event.getPointerId(event.actionIndex)
            }
            MotionEvent.ACTION_CANCEL -> {
                dpadPointers.clear()
                updateMask(0)
                return true
            }
        }

        var newMask = 0
        for (i in 0 until event.pointerCount) {
            val id = event.getPointerId(i)
            if (id == lifted) continue
            val x = event.getX(i)
            val y = event.getY(i)
            newMask = newMask or if (id in dpadPointers) dpadKeys(x, y) else buttonKeys(x, y)
        }
        if (lifted >= 0) dpadPointers.remove(lifted)
        if (action == MotionEvent.ACTION_UP) dpadPointers.clear()
        updateMask(newMask)
        return true
    }

    private fun dpadKeys(x: Float, y: Float): Int {
        val dx = x - dpadX
        val dy = y - dpadY
        val distance = hypot(dx, dy)
        if (distance < dpadRadius * 0.22f) return 0
        // An axis counts when it carries enough of the push; diagonals happen
        // between ~25° and ~65°, so straight presses stay straight.
        val threshold = distance * 0.42f
        var keys = 0
        if (dx > threshold) keys = keys or Keys.RIGHT
        if (dx < -threshold) keys = keys or Keys.LEFT
        if (dy > threshold) keys = keys or Keys.DOWN
        if (dy < -threshold) keys = keys or Keys.UP
        return keys
    }

    private fun buttonKeys(x: Float, y: Float): Int {
        var keys = 0
        for (button in buttons) {
            if (button.hit.contains(x, y)) keys = keys or button.key
        }
        return keys
    }

    private fun updateMask(newMask: Int) {
        if (newMask == mask) return
        val pressed = newMask and mask.inv()
        mask = newMask
        if (pressed != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        onKeysChanged?.invoke(newMask)
        invalidate()
    }

    /** Releases every button, e.g. when a menu opens on top. */
    fun reset() {
        dpadPointers.clear()
        updateMask(0)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val idleFill = theme.idle(overlay)
        val faceFill = theme.faceIdle(overlay)
        val pressedFill = theme.pressed(overlay)
        val outline = theme.outline(overlay)
        val label = theme.label(overlay)
        val faceLabel = theme.faceLabel(overlay)

        drawDpad(canvas, idleFill, pressedFill, outline)

        for (button in buttons) {
            val pressed = mask and button.key != 0
            fill.color = when {
                pressed -> pressedFill
                button.round -> faceFill
                else -> idleFill
            }
            stroke.color = outline
            val r = button.rect
            if (button.round) {
                canvas.drawOval(r, fill)
                canvas.drawOval(r, stroke)
            } else {
                val corner = r.height() / 2f
                canvas.drawRoundRect(r, corner, corner, fill)
                canvas.drawRoundRect(r, corner, corner, stroke)
            }
            text.color = if (button.round) faceLabel else label
            val baseline = r.centerY() - (text.descent() + text.ascent()) / 2f
            canvas.drawText(button.label, r.centerX(), baseline, text)
        }
    }

    private val armRect = RectF()
    private val crossPath = Path()
    private val armPath = Path()

    /** Both arms as one shape: one outline, and translucent fills don't add up in the middle. */
    private fun buildCross() {
        val arm = dpadRadius * 0.36f
        val reach = dpadRadius * 0.95f
        val corner = arm * 0.35f
        crossPath.reset()
        armRect.set(dpadX - arm, dpadY - reach, dpadX + arm, dpadY + reach)
        crossPath.addRoundRect(armRect, corner, corner, Path.Direction.CW)
        armPath.reset()
        armRect.set(dpadX - reach, dpadY - arm, dpadX + reach, dpadY + arm)
        armPath.addRoundRect(armRect, corner, corner, Path.Direction.CW)
        crossPath.op(armPath, Path.Op.UNION)
    }

    private fun drawDpad(canvas: Canvas, idleFill: Int, pressedFill: Int, outline: Int) {
        val arm = dpadRadius * 0.36f
        val reach = dpadRadius * 0.95f
        val corner = arm * 0.35f
        fill.color = idleFill
        canvas.drawPath(crossPath, fill)
        stroke.color = outline
        canvas.drawPath(crossPath, stroke)
        fill.color = pressedFill
        if (mask and Keys.UP != 0) {
            armRect.set(dpadX - arm, dpadY - reach, dpadX + arm, dpadY - arm)
            canvas.drawRoundRect(armRect, corner, corner, fill)
        }
        if (mask and Keys.DOWN != 0) {
            armRect.set(dpadX - arm, dpadY + arm, dpadX + arm, dpadY + reach)
            canvas.drawRoundRect(armRect, corner, corner, fill)
        }
        if (mask and Keys.LEFT != 0) {
            armRect.set(dpadX - reach, dpadY - arm, dpadX - arm, dpadY + arm)
            canvas.drawRoundRect(armRect, corner, corner, fill)
        }
        if (mask and Keys.RIGHT != 0) {
            armRect.set(dpadX + arm, dpadY - arm, dpadX + reach, dpadY + arm)
            canvas.drawRoundRect(armRect, corner, corner, fill)
        }
        canvas.drawCircle(dpadX, dpadY, arm * 0.45f, stroke)
    }
}
