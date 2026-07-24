package com.nkbe.opulse

import android.graphics.Color
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import top.yukonga.miuix.kmp.theme.ColorSchemeMode
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.theme.ThemeController

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.rgb(18, 16, 13)
        window.navigationBarColor = Color.rgb(18, 16, 13)

        setContent {
            val themeController = androidx.compose.runtime.remember {
                ThemeController(ColorSchemeMode.System)
            }
            val rootShell = androidx.compose.runtime.remember { RootShell(this@MainActivity) }
            MiuixTheme(controller = themeController) {
                CollectorDashboard(rootShell)
            }
        }
    }
}
