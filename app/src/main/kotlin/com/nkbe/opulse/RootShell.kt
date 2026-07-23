package com.nkbe.opulse

import android.content.Context
import android.util.Log
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

data class ShellResult(val exitCode: Int, val output: String) {
    val isSuccess: Boolean get() = exitCode == 0
}

class RootShell(private val context: Context) {
    companion object {
        private const val TAG = "O-Pulse"
        private const val COMMAND_TIMEOUT_SECONDS = 8L
        private const val ROOT_DIR = "/data/adb/opulse"
        private const val SCRIPT = "$ROOT_DIR/collector.sh"
        private const val STATE = "$ROOT_DIR/state.json"
        private const val PID = "$ROOT_DIR/collector.pid"
        private const val LOG = "$ROOT_DIR/run/collector.log"
        private val ROOT_BINARIES = listOf(
            "/system/bin/su",
            "/system/xbin/su",
            "/data/adb/ksu/bin/su",
            "/data/adb/sukisu/bin/su",
            "su",
        )
    }

    private fun quote(value: String): String = "'${value.replace("'", "'\\''")}'"

    fun ensureCollector(forceRestart: Boolean = false): ShellResult {
        return try {
            val localScript = File(context.filesDir, "collector.sh")
            context.assets.open("collector.sh").use { input ->
                localScript.outputStream().use { output -> input.copyTo(output) }
            }

            val command = """
                mkdir -p $ROOT_DIR
                mkdir -p $ROOT_DIR/run
                collector_pid="${'$'}(cat $PID 2>/dev/null)"
                if [ "${forceRestart}" = "false" ] && [ -f $SCRIPT ] && [ -n "${'$'}collector_pid" ] && kill -0 "${'$'}collector_pid" 2>/dev/null && [ -r "/proc/${'$'}collector_pid/cmdline" ] && tr '\000' ' ' < "/proc/${'$'}collector_pid/cmdline" | grep -Fq "$SCRIPT"; then
                    exit 0
                fi
                if [ "${forceRestart}" = "true" ] && [ -n "${'$'}collector_pid" ] && kill -0 "${'$'}collector_pid" 2>/dev/null && [ -r "/proc/${'$'}collector_pid/cmdline" ] && tr '\000' ' ' < "/proc/${'$'}collector_pid/cmdline" | grep -Fq "$SCRIPT"; then
                    kill "${'$'}collector_pid" 2>/dev/null || true
                    sleep 1
                fi
                rm -f $PID
                cp ${quote(localScript.absolutePath)} $SCRIPT
                chmod 0755 $SCRIPT
                nohup /system/bin/sh $SCRIPT --state-file $STATE --interval 3 >> $LOG 2>&1 &
                echo ${'$'}! > $PID
            """.trimIndent()
            execute(command)
        } catch (error: Exception) {
            ShellResult(-1, error.message ?: "Collector setup failed")
        }
    }

    fun readState(): ShellResult = execute("cat $STATE")

    fun readLog(): ShellResult = execute("tail -n 8 $LOG")

    private fun startRoot(command: String): Process {
        var lastError: Exception? = null
        for (binary in ROOT_BINARIES) {
            try {
                return ProcessBuilder(binary, "-c", command)
                    .redirectErrorStream(true)
                    .start()
            } catch (error: Exception) {
                lastError = error
            }
        }
        throw lastError ?: IOException("Root binary unavailable")
    }

    fun execute(command: String): ShellResult {
        return try {
            val process = startRoot(command)
            val output = StringBuilder()
            val reader = thread(isDaemon = true, name = "opulse-root-output") {
                process.inputStream.bufferedReader().use { output.append(it.readText()) }
            }
            if (!process.waitFor(COMMAND_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                process.destroyForcibly()
                reader.join(1000)
                Log.e(TAG, "Root command timed out")
                return ShellResult(-1, "Root command timed out")
            }
            reader.join(1000)
            ShellResult(process.exitValue(), output.toString().trim())
        } catch (error: Exception) {
            Log.e(TAG, "Root command failed", error)
            ShellResult(-1, error.message ?: "Root shell unavailable")
        }
    }
}
