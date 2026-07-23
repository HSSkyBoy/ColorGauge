package com.nkbe.opulse

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
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
        webView.addJavascriptInterface(AppBridge(RootShell(this)), "opulse")
        setContentView(webView)
        webView.loadUrl("file:///android_asset/webroot/index.html")
    }
}
