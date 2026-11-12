// Companion app (ADR-0023): device-side UI that needs app permissions, starting with the
// floating cursor overlay. Kotlin support is built into AGP 9.

plugins {
    alias(libs.plugins.android.application)
}

val protocolVectors = rootProject.layout.projectDirectory.dir("../protocol/vectors")

android {
    namespace = "com.gamescrcpy.companion"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.gamescrcpy.companion"
        minSdk = 28
        targetSdk = 36
        versionCode = 3
        versionName = "2.1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint {
        disable += setOf("GradleDependency", "OldTargetApi")
        warningsAsErrors = true
    }

    testOptions {
        unitTests.all {
            it.useJUnitPlatform()
            it.inputs.dir(protocolVectors).withPropertyName("protocolVectors").withPathSensitivity(PathSensitivity.RELATIVE)
            it.systemProperty("gs.protocol.vectorDir", protocolVectors.asFile.absolutePath)
        }
    }
}

dependencies {
    implementation(project(":protocol"))
    implementation(libs.kotlin.stdlib)
    implementation(libs.androidx.camera.core)
    implementation(libs.androidx.camera.camera2)
    implementation(libs.androidx.camera.lifecycle)
    implementation(libs.androidx.camera.view)
    implementation(libs.androidx.lifecycle.runtime)
    implementation(libs.zxing.core)

    testImplementation(platform(libs.junit.bom))
    testImplementation(libs.junit.jupiter)
    testImplementation(libs.org.json)
    testRuntimeOnly(libs.junit.platform.launcher)
}
