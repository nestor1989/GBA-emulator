package com.nestordelrio.emu

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

/**
 * "Apoya el proyecto": the supporter pack (cosmetics only), the theme and icon
 * pickers it unlocks, and tips. Reached from the home screen; never shown
 * during a game.
 */
class SupportActivity : Activity(), Support.Listener {

    private lateinit var content: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Support.init(this)
        content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(24), dp(20), dp(32))
        }
        val scroll = ScrollView(this).apply {
            addView(
                content,
                ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT),
            )
        }
        setContentView(scroll)
        applySafeInsets(scroll, includeSystemBars = true)
    }

    override fun onStart() {
        super.onStart()
        Support.addListener(this)
        render()
        Support.refresh()
    }

    override fun onStop() {
        super.onStop()
        Support.removeListener(this)
    }

    override fun onSupportChanged() {
        if (!isDestroyed) render()
    }

    override fun onSupportEvent(event: Support.Event) {
        if (isDestroyed) return
        when (event) {
            Support.Event.THANKS_PACK -> AlertDialog.Builder(this)
                .setTitle(R.string.support_thanks_title)
                .setMessage(R.string.support_thanks_pack)
                .setPositiveButton(android.R.string.ok, null)
                .show()
            Support.Event.THANKS_TIP -> toast(R.string.support_thanks_tip)
            Support.Event.PENDING -> toast(R.string.support_pending_toast)
            Support.Event.ERROR -> toast(R.string.support_error)
        }
    }

    private fun toast(message: Int) = Toast.makeText(this, message, Toast.LENGTH_LONG).show()

    // ---- Layout -------------------------------------------------------------

    private fun render() {
        content.removeAllViews()
        val supporter = Support.isSupporter(this)

        content.addView(text(getString(R.string.support_title), 26f, R.color.text, bold = true))
        content.addView(text(getString(R.string.support_body), 15f, R.color.text_muted).apply {
            setPadding(0, dp(8), 0, dp(20))
        })

        // Pack card
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = rounded(getColor(R.color.surface), dp(16).toFloat())
            setPadding(dp(18), dp(18), dp(18), dp(18))
        }
        card.addView(text(getString(R.string.support_pack_title), 20f, R.color.text, bold = true))
        card.addView(text(getString(R.string.support_pack_items), 15f, R.color.text).apply {
            setPadding(0, dp(10), 0, dp(6))
            setLineSpacing(dp(4).toFloat(), 1f)
        })
        card.addView(text(getString(R.string.support_pack_note), 13f, R.color.text_muted).apply {
            setPadding(0, 0, 0, dp(14))
        })
        card.addView(packButton(supporter), matchWrap())
        if (!supporter && Support.store == Support.Store.READY) {
            card.addView(text(getString(R.string.support_restore), 13f, R.color.text_muted).apply {
                gravity = Gravity.CENTER
                setPadding(dp(8), dp(12), dp(8), 0)
                setOnClickListener { restore() }
            })
        }
        content.addView(card, matchWrap())

        // Themes
        content.addView(sectionTitle(R.string.support_themes_title))
        val selectedTheme = ControlThemes.current(this)
        addGrid(ControlThemes.ALL, columns = 4) { theme ->
            val locked = theme.premium && !supporter
            cell(
                ThemeSwatch(this, theme, selected = theme === selectedTheme),
                getString(theme.nameRes),
                locked,
                height = dp(52),
            ) {
                if (locked) {
                    Toast.makeText(this, R.string.support_locked, Toast.LENGTH_SHORT).show()
                } else {
                    ControlThemes.select(this, theme)
                    render()
                }
            }
        }

        // Icons
        content.addView(sectionTitle(R.string.support_icons_title))
        val selectedIcon = AppIcons.current(this)
        addGrid(AppIcons.ALL, columns = 4) { icon ->
            val locked = icon.premium && !supporter
            val image = ImageView(this).apply {
                setImageDrawable(getDrawable(icon.iconRes))
                scaleType = ImageView.ScaleType.FIT_CENTER
                setPadding(dp(6), dp(6), dp(6), dp(6))
                background = if (icon === selectedIcon) {
                    rounded(0, dp(14).toFloat(), getColor(R.color.accent), dp(2))
                } else {
                    null
                }
            }
            cell(image, getString(icon.nameRes), locked, height = dp(68)) {
                when {
                    locked -> Toast.makeText(this, R.string.support_locked, Toast.LENGTH_SHORT).show()
                    icon !== selectedIcon -> confirmIcon(icon)
                }
            }
        }

        // Tips
        content.addView(sectionTitle(R.string.support_tips_title))
        content.addView(text(getString(R.string.support_tips_body), 14f, R.color.text_muted).apply {
            setPadding(0, 0, 0, dp(12))
        })
        val tips = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val tipLabels = mapOf(
            Support.TIP_SMALL to R.string.support_tip_small,
            Support.TIP_MEDIUM to R.string.support_tip_medium,
            Support.TIP_LARGE to R.string.support_tip_large,
        )
        for (tip in Support.TIPS) {
            val price = Support.price(tip)
            val label = getString(tipLabels.getValue(tip), price ?: "…")
            val button = actionButton(label, primary = false, enabled = price != null) { buy(tip) }
            tips.addView(
                button,
                LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply {
                    marginStart = dp(4)
                    marginEnd = dp(4)
                },
            )
        }
        content.addView(tips, matchWrap())

        content.addView(text(getString(R.string.support_footer), 12f, R.color.text_muted).apply {
            gravity = Gravity.CENTER
            setPadding(0, dp(28), 0, 0)
        })

        if (BuildConfig.DEBUG) {
            val unlocked = Support.isDebugUnlocked(this)
            content.addView(
                text(getString(if (unlocked) R.string.support_debug_lock else R.string.support_debug_unlock), 13f, R.color.accent).apply {
                    gravity = Gravity.CENTER
                    setPadding(dp(8), dp(20), dp(8), dp(8))
                    setOnClickListener { Support.setDebugUnlocked(this@SupportActivity, !unlocked) }
                },
            )
        }
    }

    private fun packButton(supporter: Boolean): Button {
        val price = Support.price(Support.PACK)
        val (label, enabled) = when {
            supporter -> getString(R.string.support_owned) to false
            Support.packPending -> getString(R.string.support_pending) to false
            Support.store == Support.Store.UNAVAILABLE -> getString(R.string.support_unavailable) to false
            Support.store == Support.Store.CONNECTING || !Support.productsLoaded ->
                getString(R.string.support_loading) to false
            price == null -> getString(R.string.support_product_missing) to false
            else -> getString(R.string.support_buy, price) to true
        }
        return actionButton(label, primary = !supporter, enabled = enabled) { buy(Support.PACK) }.apply {
            if (supporter) {
                alpha = 1f
                setTextColor(getColor(R.color.supporter))
            }
        }
    }

    private fun buy(productId: String) {
        if (!Support.buy(this, productId)) toast(R.string.support_error)
    }

    private fun restore() {
        Support.refresh { owned ->
            if (isDestroyed) return@refresh
            toast(
                when (owned) {
                    true -> R.string.support_restore_ok
                    false -> R.string.support_restore_none
                    null -> R.string.support_restore_error
                },
            )
        }
    }

    private fun confirmIcon(icon: AppIcons.Icon) {
        AlertDialog.Builder(this)
            .setTitle(R.string.support_icon_confirm_title)
            .setMessage(R.string.support_icon_confirm_body)
            .setPositiveButton(R.string.support_icon_change) { _, _ ->
                AppIcons.select(this, icon)
                render()
            }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }

    // ---- Small view helpers ---------------------------------------------------

    private fun matchWrap() =
        LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)

    private fun text(value: String, sizeSp: Float, colorRes: Int, bold: Boolean = false) = TextView(this).apply {
        text = value
        setTextColor(getColor(colorRes))
        setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
        if (bold) typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
    }

    private fun sectionTitle(res: Int) = text(getString(res), 18f, R.color.text, bold = true).apply {
        setPadding(0, dp(28), 0, dp(12))
    }

    private fun actionButton(label: String, primary: Boolean, enabled: Boolean, onClick: () -> Unit) =
        Button(this).apply {
            text = label
            isAllCaps = false
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            setTextColor(getColor(R.color.text))
            background = if (primary) {
                rounded(getColor(R.color.accent), dp(14).toFloat())
            } else {
                rounded(getColor(R.color.surface), dp(14).toFloat(), getColor(R.color.text_muted), dp(1))
            }
            stateListAnimator = null
            minHeight = dp(52)
            setPadding(dp(12), dp(10), dp(12), dp(10))
            isEnabled = enabled
            alpha = if (enabled) 1f else 0.55f
            setOnClickListener { onClick() }
        }

    private fun <T> addGrid(items: List<T>, columns: Int, makeCell: (T) -> View) {
        for (start in items.indices step columns) {
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            for (i in start until start + columns) {
                val params = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply {
                    marginStart = dp(4)
                    marginEnd = dp(4)
                    bottomMargin = dp(12)
                }
                // Empty cells keep the last row aligned with the others.
                row.addView(if (i < items.size) makeCell(items[i]) else View(this), params)
            }
            content.addView(row, matchWrap())
        }
    }

    private fun cell(preview: View, name: String, locked: Boolean, height: Int, onClick: () -> Unit) =
        LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            addView(preview, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, height))
            addView(
                text(if (locked) "🔒 $name" else name, 12f, if (locked) R.color.text_muted else R.color.text).apply {
                    gravity = Gravity.CENTER
                    maxLines = 1
                    setPadding(0, dp(6), 0, 0)
                },
            )
            if (locked) preview.alpha = 0.55f
            isClickable = true
            setOnClickListener { onClick() }
        }
}

/** A tiny pad (cross + two buttons) painted with a [ControlTheme]. */
private class ThemeSwatch(context: Context, private val theme: ControlTheme, private val selected: Boolean) :
    View(context) {

    private val density = resources.displayMetrics.density
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val box = RectF()
    private val cross = Path()
    private val bar = Path()

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        val border = (if (selected) 3f else 1f) * density
        val radius = 12f * density

        box.set(border / 2f, border / 2f, w - border / 2f, h - border / 2f)
        paint.style = Paint.Style.FILL
        paint.color = theme.panel
        canvas.drawRoundRect(box, radius, radius, paint)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = border
        paint.color = if (selected) context.getColor(R.color.accent) else 0x40FFFFFF
        canvas.drawRoundRect(box, radius, radius, paint)

        // Cross on the left third, A/B on the right.
        val cy = h / 2f
        val cx = w * 0.3f
        val reach = h * 0.28f
        val arm = reach * 0.38f
        // One outline around the whole cross; translucent fills must not overlap.
        cross.reset()
        cross.addRect(cx - arm, cy - reach, cx + arm, cy + reach, Path.Direction.CW)
        bar.reset()
        bar.addRect(cx - reach, cy - arm, cx + reach, cy + arm, Path.Direction.CW)
        cross.op(bar, Path.Op.UNION)
        paint.style = Paint.Style.FILL
        paint.color = theme.button
        canvas.drawPath(cross, paint)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = density
        paint.color = theme.outline
        canvas.drawPath(cross, paint)

        val r = h * 0.13f
        for ((x, y) in listOf(w * 0.78f to cy - r * 0.7f, w * 0.62f to cy + r * 0.8f)) {
            paint.style = Paint.Style.FILL
            paint.color = theme.face
            canvas.drawCircle(x, y, r, paint)
            paint.style = Paint.Style.STROKE
            paint.color = theme.outline
            canvas.drawCircle(x, y, r, paint)
        }
    }
}
