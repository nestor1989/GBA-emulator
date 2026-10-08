package com.nestordelrio.emu

import android.app.Activity
import android.graphics.Typeface
import android.os.Bundle
import android.text.method.LinkMovementMethod
import android.text.util.Linkify
import android.util.TypedValue
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/**
 * Open source notices. Lists the exact mGBA commit in this build and where to
 * get its source (MPL 2.0 §3.2a), plus the full license texts.
 */
class LicensesActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(20), dp(20), dp(32))
        }
        column.addView(TextView(this).apply {
            text = getString(R.string.licenses_title)
            setTextColor(getColor(R.color.text))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 24f)
            typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
            setPadding(0, 0, 0, dp(16))
        })

        val commit = BuildConfig.MGBA_COMMIT
        // NOTICE-<language>.txt when there is one; English otherwise.
        val language = java.util.Locale.getDefault().language
        val localized = readAsset("licenses/NOTICE-$language.txt")
        val notice = localized.ifEmpty { readAsset("licenses/NOTICE.txt") }
            .replace("{MGBA_COMMIT}", commit)
            .replace("{MGBA_SOURCE_URL}", "https://github.com/mgba-emu/mgba/tree/$commit")
        column.addView(body(notice, monospace = false))

        val texts = assets.list("licenses").orEmpty().filter { !it.startsWith("NOTICE") }.sorted()
        for (name in texts) {
            column.addView(TextView(this).apply {
                text = name.removeSuffix(".txt")
                setTextColor(getColor(R.color.text))
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f)
                typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
                setPadding(0, dp(28), 0, dp(8))
            })
            column.addView(body(readAsset("licenses/$name"), monospace = true))
        }

        val scroll = ScrollView(this).apply { addView(column) }
        setContentView(scroll)
        applySafeInsets(scroll, includeSystemBars = true)
    }

    private fun body(content: String, monospace: Boolean) = TextView(this).apply {
        text = content
        setTextColor(getColor(R.color.text_muted))
        setTextSize(TypedValue.COMPLEX_UNIT_SP, if (monospace) 11f else 14f)
        if (monospace) typeface = Typeface.MONOSPACE
        setTextIsSelectable(true)
        Linkify.addLinks(this, Linkify.WEB_URLS)
        movementMethod = LinkMovementMethod.getInstance()
    }

    private fun readAsset(path: String): String =
        runCatching { assets.open(path).bufferedReader().use { it.readText() } }.getOrDefault("")
}
