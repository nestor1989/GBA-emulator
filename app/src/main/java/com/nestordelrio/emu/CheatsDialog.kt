package com.nestordelrio.emu

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Typeface
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.ViewGroup
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import com.nestordelrio.emu.core.Emulator

/**
 * List of the game's cheats with on/off switches, plus an editor to add one.
 * Codes are checked by mGBA before saving (GameShark, Action Replay and
 * CodeBreaker formats are detected automatically).
 */
class CheatsDialog(
    private val activity: Activity,
    private val store: CheatStore,
    private val onChanged: (List<Cheat>) -> Unit,
) {
    private val cheats = store.load()
    private lateinit var list: LinearLayout

    fun create(): AlertDialog {
        list = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(activity.dp(20), activity.dp(8), activity.dp(20), activity.dp(8))
        }
        val scroll = ScrollView(activity).apply { addView(list) }
        render()
        val dialog = AlertDialog.Builder(activity)
            .setTitle(R.string.cheats_title)
            .setView(scroll)
            .setNeutralButton(R.string.cheats_add, null)
            .setPositiveButton(R.string.cheats_done, null)
            .create()
        dialog.setOnShowListener {
            // Keep this dialog open (and the game paused) while the editor is on top.
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener { showEditor() }
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener { dialog.dismiss() }
        }
        return dialog
    }

    private fun render() {
        list.removeAllViews()
        if (cheats.isEmpty()) {
            list.addView(TextView(activity).apply {
                text = activity.getString(R.string.cheats_empty)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
                setPadding(0, activity.dp(8), 0, activity.dp(8))
            })
            return
        }
        cheats.forEachIndexed { index, cheat ->
            val row = LinearLayout(activity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            row.addView(
                CheckBox(activity).apply {
                    text = label(cheat, index)
                    isChecked = cheat.enabled
                    setOnCheckedChangeListener { _, checked ->
                        cheats[index] = cheats[index].copy(enabled = checked)
                        commit()
                    }
                },
                LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f),
            )
            row.addView(TextView(activity).apply {
                text = "✕"
                contentDescription = activity.getString(R.string.cheat_delete)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f)
                setPadding(activity.dp(16), activity.dp(10), activity.dp(4), activity.dp(10))
                setOnClickListener { confirmDelete(index) }
            })
            list.addView(row)
        }
    }

    private fun commit() {
        store.save(cheats)
        onChanged(cheats.toList())
    }

    private fun label(cheat: Cheat, index: Int): String =
        cheat.name.ifEmpty { activity.getString(R.string.cheat_unnamed, index + 1) }

    private fun showEditor() {
        val name = EditText(activity).apply {
            hint = activity.getString(R.string.cheat_name_hint)
            setSingleLine()
        }
        val code = EditText(activity).apply {
            hint = activity.getString(R.string.cheat_code_hint)
            minLines = 3
            gravity = Gravity.TOP or Gravity.START
            typeface = Typeface.MONOSPACE
            inputType = InputType.TYPE_CLASS_TEXT or
                InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS or
                InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
        }
        val form = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(activity.dp(20), activity.dp(8), activity.dp(20), 0)
            addView(name)
            addView(code)
        }
        val editor = AlertDialog.Builder(activity)
            .setTitle(R.string.cheat_new_title)
            .setView(form)
            .setPositiveButton(R.string.cheat_save, null)
            .setNegativeButton(R.string.action_cancel, null)
            .create()
        editor.setOnShowListener {
            editor.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val text = code.text.toString().trim()
                if (text.isEmpty() || !Emulator.isValidCheat(text)) {
                    code.error = activity.getString(R.string.cheat_invalid)
                    return@setOnClickListener
                }
                cheats.add(Cheat(name.text.toString().trim(), text, enabled = true))
                commit()
                render()
                editor.dismiss()
            }
        }
        editor.show()
    }

    private fun confirmDelete(index: Int) {
        val cheat = cheats.getOrNull(index) ?: return
        AlertDialog.Builder(activity)
            .setMessage(activity.getString(R.string.cheat_delete_confirm, label(cheat, index)))
            .setPositiveButton(R.string.cheat_delete) { _, _ ->
                if (index < cheats.size) {
                    cheats.removeAt(index)
                    commit()
                    render()
                }
            }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }
}
