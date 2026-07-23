package com.nkbe.opulse

import android.webkit.JavascriptInterface
import org.json.JSONObject

class AppBridge(private val shell: RootShell) {
    @Volatile
    private var collectorReady = false

    @JavascriptInterface
    @Synchronized
    fun exec(command: String): String {
        if (!collectorReady) {
            val started = shell.ensureCollector()
            if (!started.isSuccess) return result(started)
            collectorReady = true
        }
        return result(shell.execute(command))
    }

    private fun result(shellResult: ShellResult): String {
        return JSONObject()
            .put("errno", shellResult.exitCode)
            .put("stdout", shellResult.output)
            .put("stderr", if (shellResult.isSuccess) "" else shellResult.output)
            .toString()
    }
}
