# JNI symbols are resolved by class and method name: never rename them.
-keepclasseswithmembernames class com.nestordelrio.emu.core.NativeBridge {
    native <methods>;
}
