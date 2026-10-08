import java.io.File

plugins {
    alias(libs.plugins.android.application)
}

// Exact mGBA commit compiled into this build. The licenses screen links to its
// source, which is how the app meets MPL 2.0 §3.2(a). Read from the submodule's
// git metadata, so it never goes stale when the pin moves.
fun mgbaCommit(root: File): String {
    val submodule = File(root, "third_party/mgba")
    val dotGit = File(submodule, ".git")
    val gitDir: File = when {
        dotGit.isDirectory -> dotGit
        dotGit.isFile -> {
            // Submodules keep a "gitdir: ../../.git/modules/..." pointer file.
            val target = File(dotGit.readText().trim().removePrefix("gitdir:").trim())
            if (target.isAbsolute) target else File(submodule, target.path)
        }
        else -> return "unknown"
    }
    val head = File(gitDir, "HEAD").takeIf { it.isFile }?.readText()?.trim() ?: return "unknown"
    if (!head.startsWith("ref:")) return head
    val ref = head.removePrefix("ref:").trim()
    File(gitDir, ref).takeIf { it.isFile }?.let { return it.readText().trim() }
    return File(gitDir, "packed-refs").takeIf { it.isFile }?.readLines()
        ?.firstOrNull { it.endsWith(" $ref") }?.substringBefore(" ") ?: "unknown"
}

android {
    namespace = "com.nestordelrio.emu"
    compileSdk = 36

    defaultConfig {
        // Final (2026-10-06): the app's identity on Google Play. Never change it
        // after the first upload. The code namespace above stays as it is.
        applicationId = "com.nestordelrio.playagain"
        minSdk = 24
        targetSdk = 36
        // versionCode: the number Play compares; +1 on every upload, never reused.
        // versionName: what users see. 1.0.0 = first public release.
        versionCode = 2
        versionName = "1.0.0"
        buildConfigField("String", "MGBA_COMMIT", "\"${mgbaCommit(rootDir)}\"")
    }

    buildTypes {
        getByName("debug") {
            applicationIdSuffix = ".debug"
        }
        getByName("release") {
            isMinifyEnabled = false
            // Puts the native debug symbols (mGBA, JNI, audio) in the AAB. Play
            // Console uses them to show crash stack traces with function names,
            // files and lines. They are not part of what users download.
            ndk {
                debugSymbolLevel = "FULL"
            }
        }
    }

    buildFeatures {
        buildConfig = true
    }

    // Lists the app's languages for Android 13+ per-app language settings.
    // The default resources (values/) are English: see res/resources.properties.
    androidResources {
        generateLocaleConfig = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    implementation(project(":core"))
    // Supporter pack and tips. Cosmetic only: no feature is ever behind a purchase.
    implementation(libs.billing)
}

// Guardrail: no third-party ROM may ship in a release build. The only game
// allowed is our own Cometa (homebrew/cometa), checked by path and header title.
val checkNoRomsInRelease = tasks.register("checkNoRomsInRelease") {
    val assetDirs = listOf("src/main/assets", "src/release/assets").map { file(it) }
    doLast {
        val romExtensions = setOf("gba", "agb", "gb", "gbc", "sgb", "bin", "zip", "7z", "rar")
        fun isCometa(dir: File, rom: File): Boolean {
            if (rom.relativeTo(dir).invariantSeparatorsPath != "games/cometa.gba") return false
            val bytes = rom.readBytes()
            return bytes.size > 0xC0 && String(bytes, 0xA0, 6, Charsets.US_ASCII) == "COMETA"
        }
        val found = assetDirs.filter { it.isDirectory }.flatMap { dir ->
            dir.walkTopDown()
                .filter { it.isFile && it.extension.lowercase() in romExtensions && !isCometa(dir, it) }
                .toList()
        }
        if (found.isNotEmpty()) {
            throw GradleException("ROM files in release assets: ${found.joinToString()}")
        }
    }
}
tasks.configureEach {
    if (name == "preReleaseBuild") dependsOn(checkNoRomsInRelease)
}
