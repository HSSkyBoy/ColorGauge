import org.gradle.api.tasks.Copy
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "top.nkbe.opulse"
    compileSdk = 37

    defaultConfig {
        applicationId = "top.nkbe.opulse"
        minSdk = 26
        targetSdk = 37
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
}

val syncCollectorScript = tasks.register<Copy>("syncCollectorScript") {
    from(rootProject.file("collector.sh"))
    into(project.file("src/main/assets"))
    rename { "collector.sh" }
}

val syncWebAssets = tasks.register<Copy>("syncWebAssets") {
    from(rootProject.file("webroot"))
    into(project.file("src/main/assets/webroot"))
}

tasks.named("preBuild") {
    dependsOn(syncCollectorScript, syncWebAssets)
}
