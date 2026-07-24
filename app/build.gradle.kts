import org.gradle.api.tasks.Copy
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "top.nkbe.opulse"
    compileSdk = 36
    buildToolsVersion = "36.1.0"

    defaultConfig {
        applicationId = "top.nkbe.opulse"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

}

kotlin {
    compilerOptions {
        jvmTarget = JvmTarget.JVM_21
    }

dependencies {
    implementation("top.yukonga.miuix.kmp:miuix-ui-android:0.9.3")
    implementation("androidx.activity:activity-compose:1.10.1")
}

val syncCollectorScript = tasks.register<Copy>("syncCollectorScript") {
    from(rootProject.file("collector.sh"))
    into(project.file("src/main/assets"))
    rename { "collector.sh" }
}

tasks.named("preBuild") {
    dependsOn(syncCollectorScript)
}
