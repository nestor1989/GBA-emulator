plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "com.nestordelrio.emu.core"
    compileSdk = 36
    // NDK r28+ aligns native libraries to 16 KB pages by default (required by Google Play).
    ndkVersion = "28.2.13676358"

    defaultConfig {
        minSdk = 24
        consumerProguardFiles("consumer-rules.pro")

        ndk {
            // x86_64 is for the Android Studio emulator and Chromebooks.
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
