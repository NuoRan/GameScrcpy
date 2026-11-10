// Pure Java wire-protocol codecs shared by the server and the companion app.
// No Android dependency, so the golden-vector tests run on a plain JVM.

plugins {
    `java-library`
}

tasks.withType<JavaCompile>().configureEach {
    options.release.set(17)
    options.encoding = "UTF-8"
    options.compilerArgs.addAll(listOf("-Xlint:all", "-Werror"))
}

dependencies {
    testImplementation(platform(libs.junit.bom))
    testImplementation(libs.junit.jupiter)
    testImplementation(libs.org.json)
    testRuntimeOnly(libs.junit.platform.launcher)
}

val protocolVectors = rootProject.layout.projectDirectory.dir("../protocol/vectors")

tasks.test {
    useJUnitPlatform()
    inputs.dir(protocolVectors).withPropertyName("protocolVectors").withPathSensitivity(PathSensitivity.RELATIVE)
    systemProperty("gs.protocol.vectorDir", protocolVectors.asFile.absolutePath)
}
