// Device-side server (ADR-0003, ADR-0017). Packaged as an APK only to obtain a dex; it has no
// components and is launched by the desktop client through `app_process` as the shell user.

plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.gamescrcpy.server"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.gamescrcpy.server"
        minSdk = 28
        targetSdk = 36
        versionCode = 1
        versionName = "2.0.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        aidl = true
    }

    lint {
        disable += setOf("MissingApplicationIcon", "DataExtractionRules", "AllowBackup", "PrivateApi",
                         "DiscouragedPrivateApi", "OldTargetApi")
        warningsAsErrors = true
    }

    testOptions {
        unitTests.all { it.useJUnitPlatform() }
    }
}

tasks.withType<JavaCompile>().configureEach {
    options.compilerArgs.addAll(listOf("-Xlint:all", "-Werror"))
}

abstract class WriteInteropArguments : DefaultTask() {
    @get:Classpath
    abstract val classpath: ConfigurableFileCollection

    @get:OutputFile
    abstract val output: RegularFileProperty

    @TaskAction
    fun write() {
        val path = classpath.asPath.replace('\\', '/')
        output.get().asFile.writeText("-cp \"$path\"\ncom.gamescrcpy.server.transport.DirectInteropServer\n")
    }
}

val writeInteropArguments = tasks.register<WriteInteropArguments>("writeInteropArguments") {
    classpath.from(tasks.named<Test>("testDebugUnitTest").map { it.classpath })
    output.set(layout.buildDirectory.file("interop/server.args"))
}
tasks.named("build") { dependsOn(writeInteropArguments) }

dependencies {
    implementation(project(":protocol"))

    testImplementation(platform(libs.junit.bom))
    testImplementation(libs.junit.jupiter)
    testRuntimeOnly(libs.junit.platform.launcher)
}
