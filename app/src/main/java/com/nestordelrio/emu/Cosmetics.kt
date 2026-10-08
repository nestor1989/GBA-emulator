package com.nestordelrio.emu

import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager

/**
 * Colors for the on-screen controls. Original palettes: no console colorways
 * or other trade dress.
 *
 * Portrait uses the colors as given (the controls sit on [panel]). Landscape
 * draws translucent controls over the game, derived from [tint].
 */
class ControlTheme(
    val id: String,
    val nameRes: Int,
    val premium: Boolean,
    /** Background of the portrait panel and quick bar. */
    val panel: Int,
    /** Idle fill: cross, L/R, SELECT/START and quick-bar chips. */
    val button: Int,
    /** Idle fill of A and B. */
    val face: Int,
    val pressed: Int,
    val outline: Int,
    /** Button labels and chip text. */
    val label: Int,
    /** Active Turbo chip. */
    val accent: Int,
    val onAccent: Int,
    /** Landscape: base color of the translucent controls. */
    val tint: Int,
    /** Labels on A and B, for faces too bright for [label]. */
    val faceLabel: Int = label,
) {
    private fun alpha(color: Int, alpha: Int) = (color and 0x00FFFFFF) or (alpha shl 24)

    private val tintedFaces = face != button

    fun idle(overlay: Boolean) = if (overlay) alpha(tint, 0x33) else button
    fun faceIdle(overlay: Boolean) = when {
        !overlay -> face
        tintedFaces -> alpha(accent, 0x55)
        else -> alpha(tint, 0x33)
    }
    fun pressed(overlay: Boolean) = if (overlay) alpha(tint, 0x88) else pressed
    fun outline(overlay: Boolean) = if (overlay) alpha(tint, 0x66) else outline
    fun label(overlay: Boolean) = if (overlay) alpha(tint, 0xCC) else label
    fun faceLabel(overlay: Boolean) = if (overlay) alpha(tint, 0xCC) else faceLabel

    fun chipFill(overlay: Boolean) = if (overlay) alpha(tint, 0x26) else button
    fun chipStroke(overlay: Boolean) = if (overlay) alpha(tint, 0x40) else outline
    fun chipText(overlay: Boolean) = if (overlay) alpha(tint, 0xF2) else label
}

object ControlThemes {

    /** The original look; always free. */
    val CLASSIC = ControlTheme(
        id = "classic", nameRes = R.string.theme_classic, premium = false,
        panel = 0xFF121418.toInt(),
        button = 0x26FFFFFF, face = 0x26FFFFFF, pressed = 0x77FFFFFF,
        outline = 0x55FFFFFF, label = 0xE6FFFFFF.toInt(),
        accent = 0xFF4F8CFF.toInt(), onAccent = 0xFFFFFFFF.toInt(), tint = 0xFFFFFFFF.toInt(),
    )

    val ALL: List<ControlTheme> = listOf(
        CLASSIC,
        ControlTheme(
            id = "midnight", nameRes = R.string.theme_midnight, premium = true,
            panel = 0xFF0A1024.toInt(),
            button = 0xFF1A2547.toInt(), face = 0xFF2D52B0.toInt(), pressed = 0xFF5B86F0.toInt(),
            outline = 0xFF34477E.toInt(), label = 0xFFDCE6FF.toInt(),
            accent = 0xFF4F7BFF.toInt(), onAccent = 0xFFFFFFFF.toInt(), tint = 0xFFBFD0FF.toInt(),
        ),
        ControlTheme(
            id = "mint", nameRes = R.string.theme_mint, premium = true,
            panel = 0xFF0D1A17.toInt(),
            button = 0xFF173029.toInt(), face = 0xFF1E8A68.toInt(), pressed = 0xFF3FD3A2.toInt(),
            outline = 0xFF2A5247.toInt(), label = 0xFFDFFAF1.toInt(),
            accent = 0xFF1E9A73.toInt(), onAccent = 0xFFFFFFFF.toInt(), tint = 0xFFC8FFEE.toInt(),
        ),
        ControlTheme(
            id = "coral", nameRes = R.string.theme_coral, premium = true,
            panel = 0xFF1A1211.toInt(),
            button = 0xFF34201D.toInt(), face = 0xFFD95B44.toInt(), pressed = 0xFFFF8A73.toInt(),
            outline = 0xFF5A3530.toInt(), label = 0xFFFFE8E2.toInt(),
            accent = 0xFFD95B44.toInt(), onAccent = 0xFFFFFFFF.toInt(), tint = 0xFFFFD6CC.toInt(),
        ),
        ControlTheme(
            id = "amber", nameRes = R.string.theme_amber, premium = true,
            panel = 0xFF19130B.toInt(),
            button = 0xFF33261A.toInt(), face = 0xFFE09A2E.toInt(), pressed = 0xFFFFC266.toInt(),
            outline = 0xFF5A4128.toInt(), label = 0xFFFFF1DD.toInt(),
            accent = 0xFFC77F12.toInt(), onAccent = 0xFFFFFFFF.toInt(), tint = 0xFFFFE3B8.toInt(),
            faceLabel = 0xFF2A1A05.toInt(),
        ),
        ControlTheme(
            id = "sand", nameRes = R.string.theme_sand, premium = true,
            panel = 0xFFEDE3D1.toInt(),
            button = 0xFFDCCDB2.toInt(), face = 0xFFC9874A.toInt(), pressed = 0xFFB89E75.toInt(),
            outline = 0xFFB4A083.toInt(), label = 0xFF3B2F22.toInt(),
            accent = 0xFF9E5F22.toInt(), onAccent = 0xFFFFFFFF.toInt(), tint = 0xFFF6EBD6.toInt(),
        ),
        ControlTheme(
            id = "carbon", nameRes = R.string.theme_carbon, premium = true,
            panel = 0xFF000000.toInt(),
            button = 0xFF181818.toInt(), face = 0xFF2C2C2C.toInt(), pressed = 0xFF6E6E6E.toInt(),
            outline = 0xFF3A3A3A.toInt(), label = 0xFFF5F5F5.toInt(),
            accent = 0xFFEDEDED.toInt(), onAccent = 0xFF000000.toInt(), tint = 0xFFFFFFFF.toInt(),
        ),
    )

    private const val PREFS = "cosmetics"
    private const val KEY_THEME = "control_theme"

    /** The theme to draw. Falls back to Clásico if a premium theme is no longer owned. */
    fun current(context: Context): ControlTheme {
        val id = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_THEME, null)
        val theme = ALL.firstOrNull { it.id == id } ?: CLASSIC
        return if (theme.premium && !Support.isSupporter(context)) CLASSIC else theme
    }

    fun select(context: Context, theme: ControlTheme) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY_THEME, theme.id).apply()
    }
}

/**
 * Launcher icons, one activity-alias each in the manifest. Exactly one alias is
 * enabled at a time; switching enables the new one before disabling the rest so
 * the app never has zero launcher entries.
 */
object AppIcons {

    class Icon(val alias: String, val nameRes: Int, val iconRes: Int, val premium: Boolean)

    val CLASSIC = Icon("LauncherClassic", R.string.icon_classic, R.mipmap.ic_launcher, premium = false)

    val ALL: List<Icon> = listOf(
        CLASSIC,
        Icon("LauncherNight", R.string.icon_night, R.mipmap.ic_launcher_night, premium = true),
        Icon("LauncherCoral", R.string.icon_coral, R.mipmap.ic_launcher_coral, premium = true),
    )

    // Aliases are declared relative to the namespace, which differs from the
    // applicationId in debug builds (".debug" suffix).
    private val NAMESPACE = HomeActivity::class.java.name.substringBeforeLast('.')

    private fun component(context: Context, icon: Icon) = ComponentName(context, "$NAMESPACE.${icon.alias}")

    fun current(context: Context): Icon {
        val pm = context.packageManager
        return ALL.firstOrNull { icon ->
            when (pm.getComponentEnabledSetting(component(context, icon))) {
                PackageManager.COMPONENT_ENABLED_STATE_ENABLED -> true
                PackageManager.COMPONENT_ENABLED_STATE_DEFAULT -> icon === CLASSIC // manifest default
                else -> false
            }
        } ?: CLASSIC
    }

    fun select(context: Context, icon: Icon) {
        if (current(context) === icon) return
        val pm = context.packageManager
        pm.setComponentEnabledSetting(
            component(context, icon),
            PackageManager.COMPONENT_ENABLED_STATE_ENABLED,
            PackageManager.DONT_KILL_APP,
        )
        for (other in ALL) {
            if (other === icon) continue
            pm.setComponentEnabledSetting(
                component(context, other),
                PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
                PackageManager.DONT_KILL_APP,
            )
        }
    }
}

object Cosmetics {
    /** Back to the free look, e.g. after a refund. */
    fun resetToFree(context: Context) {
        ControlThemes.select(context, ControlThemes.CLASSIC)
        AppIcons.select(context, AppIcons.CLASSIC)
    }
}
