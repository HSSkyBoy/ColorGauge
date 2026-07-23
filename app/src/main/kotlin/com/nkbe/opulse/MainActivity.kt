package com.nkbe.opulse

import android.app.Activity
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.view.WindowInsets
import android.webkit.WebView
import android.webkit.WebViewClient

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.rgb(18, 16, 13)
        window.navigationBarColor = Color.rgb(18, 16, 13)

        val webView = WebView(this)
        webView.settings.javaScriptEnabled = true
        webView.settings.domStorageEnabled = true
        webView.settings.allowFileAccess = true
        webView.webViewClient = WebViewClient()
        WebView.setWebContentsDebuggingEnabled(true)
        webView.setOnApplyWindowInsetsListener { view, insets ->
            val top: Int
            val bottom: Int
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val bars = insets.getInsets(WindowInsets.Type.systemBars())
                top = bars.top
                bottom = bars.bottom
            } else {
                @Suppress("DEPRECATION")
                val topInset = insets.systemWindowInsetTop
                @Suppress("DEPRECATION")
                val bottomInset = insets.systemWindowInsetBottom
                top = topInset
                bottom = bottomInset
            }
            view.setPadding(view.paddingLeft, top, view.paddingRight, bottom)
            insets
        }
        webView.addJavascriptInterface(AppBridge(RootShell(this)), "opulse")
        setContentView(webView)
        webView.requestApplyInsets()
        webView.loadUrl("file:///android_asset/webroot/index.html")
    }
}
