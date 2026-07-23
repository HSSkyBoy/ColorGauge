package com.nkbe.opulse

import android.content.Context
import java.io.File

data class ShellResult(val exitCode: Int, val output: String) {
    val isSuccess: Boolean get() = exitCode == 0
}

class RootShell(private val context: Context) {
    companion object {
        private const val ROOT_DIR = "/data/adb/opulse"
        private const val SCRIPT = "$ROOT_DIR/collector.sh"
        private const val STATE = "$ROOT_DIR/state.json"
        private const val PID = "$ROOT_DIR/collector.pid"
        private const val LOG = "$ROOT_DIR/collector.log"
    }

    private fun quote(value: String): String = "'${value.replace("'", "'\\''")}'"

    fun ensureCollector(): ShellResult {
        val localScript = File(context.filesDir, "collector.sh")
        context.assets.open("collector.sh").use { input ->
            localScript.outputStream().use { output -> input.copyTo(output) }
        }

        val command = """
            mkdir -p $ROOT_DIR
            cp ${quote(localScript.absolutePath)} $SCRIPT
            chmod 0755 $SCRIPT
            if [ -f $PID ] && kill -0 "${'$'}(cat $PID 2>/dev/null)" 2>/dev/null; then
                exit 0
            fi
            rm -f $PID
            nohup /system/bin/sh $SCRIPT --state-file $STATE --interval 3 >> $LOG 2>&1 &
            echo ${'$'}! > $PID
        """.trimIndent()
        return execute(command)
    }

    fun readState(): ShellResult = execute("cat $STATE")

    fun readLog(): ShellResult = execute("tail -n 8 $LOG")

    fun execute(command: String): ShellResult {
        return try {
            val process = ProcessBuilder("su", "-c", command)
                .redirectErrorStream(true)
                .start()
            val output = process.inputStream.bufferedReader().use { it.readText() }
            ShellResult(process.waitFor(), output.trim())
        } catch (error: Exception) {
            ShellResult(-1, error.message ?: "Root shell unavailable")
        }
    }
}
