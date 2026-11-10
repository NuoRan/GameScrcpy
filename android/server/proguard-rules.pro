# app_process looks the entry point up by name.
-keep class com.gamescrcpy.server.Main {
    public static void main(java.lang.String[]);
}

-keep class android.view.IDisplayWindowListener { *; }
-keep class android.view.IDisplayWindowListener$Stub { *; }
