package com.nkbe.opulse

import android.webkit.JavascriptInterface
import org.json.JSONObject

class AppBridge(private val shell: RootShell) {
    private var collectorInitialized = false

    @JavascriptInterface
    @Synchronized
    fun exec(command: String): String {
        val started = shell.ensureCollector(forceRestart = !collectorInitialized)
        if (!started.isSuccess) return result(started)
        collectorInitialized = true
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
