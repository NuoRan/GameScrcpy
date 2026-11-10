package com.gamescrcpy.companion

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Build
import androidx.annotation.RequiresApi
import java.io.DataInputStream
import java.io.IOException
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.Socket
import java.security.SecureRandom
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

enum class LanState { IDLE, PAIRING, SEARCHING, CONNECTED, FAILED }

data class LanAddress(val host: String, val port: Int)

fun interface Discovery {
    fun find(names: Set<String>, timeoutMillis: Long): Map<String, LanAddress>
}

interface PairingSource {
    val phone: ByteArray

    fun all(): List<Pairing>

    fun remember(latest: Pairing)
}

class LanConnector(
    private val pairings: PairingSource,
    private val discovery: Discovery,
    private val deliver: (LanConnection) -> Unit,
    private val events: Events,
    private val timing: Timing = Timing(),
) : AutoCloseable {

    data class Timing(
        val connectMillis: Int = 3_000,
        val handshakeMillis: Int = 5_000,
        val discoverMillis: Long = 4_000,
        val firstBackoffMillis: Long = 2_000,
        val mostBackoffMillis: Long = 30_000,
        val stableMillis: Long = 10_000,
    )

    interface Events {
        fun state(state: LanState)

        fun paired(pairing: Pairing)
    }

    private class Joined(val connection: LanConnection, val pc: ByteArray)

    private val lock = ReentrantLock()
    private val changed = lock.newCondition()
    private val random = SecureRandom()
    private var running = true
    private var nudged = false
    private var pending: QrLink? = null
    private var worker: Thread? = null

    @Volatile
    private var current: LanConnection? = null

    fun start(link: QrLink?) {
        lock.withLock {
            if (!running) {
                return
            }
            if (link != null) {
                pending = link
            }
            nudged = true
            changed.signalAll()
            if (worker == null) {
                worker = Thread(::run, "companion-lan").also {
                    it.isDaemon = true
                    it.start()
                }
            }
        }
        if (link != null) {
            current?.close()
        }
    }

    fun nudge() {
        lock.withLock {
            nudged = true
            changed.signalAll()
        }
    }

    override fun close() {
        lock.withLock {
            running = false
            changed.signalAll()
        }
        current?.close()
    }

    private fun run() {
        var backoff = timing.firstBackoffMillis
        while (isRunning()) {
            val link = takePending()
            val connection = if (link != null) {
                events.state(LanState.PAIRING)
                pair(link)
            } else {
                events.state(LanState.SEARCHING)
                reconnect()
            }
            if (connection != null) {
                if (hasPending()) {
                    connection.close()
                    continue
                }
                current = connection
                if (!isRunning()) {
                    connection.close()
                    current = null
                    continue
                }
                events.state(LanState.CONNECTED)
                val began = System.nanoTime()
                deliver(connection)
                connection.awaitClosed(Long.MAX_VALUE)
                current = null
                if (System.nanoTime() - began >= TimeUnit.MILLISECONDS.toNanos(timing.stableMillis)) {
                    backoff = timing.firstBackoffMillis
                    continue
                }
                events.state(LanState.SEARCHING)
                pause(backoff)
                backoff = (backoff * 2).coerceAtMost(timing.mostBackoffMillis)
                continue
            }
            val exhausted = lock.withLock {
                val none = pending == null && pairings.all().isEmpty()
                if (none) {
                    worker = null
                }
                none
            }
            if (exhausted) {
                events.state(if (link != null) LanState.FAILED else LanState.IDLE)
                return
            }
            events.state(if (link != null) LanState.FAILED else LanState.SEARCHING)
            pause(backoff)
            backoff = (backoff * 2).coerceAtMost(timing.mostBackoffMillis)
        }
        lock.withLock { worker = null }
    }

    private fun pair(link: QrLink): LanConnection? {
        val joined = join(link.host, link.port, link.token, link.key, null) ?: return null
        val pairing = Pairing(Hex.encode(joined.pc), Hex.encode(link.key), link.host, link.port)
        pairings.remember(pairing)
        events.paired(pairing)
        return joined.connection
    }

    private fun reconnect(): LanConnection? {
        val known = pairings.all()
        if (known.isEmpty()) {
            return null
        }
        for (pairing in known) {
            attempt(pairing, pairing.host, pairing.port)?.let { return it }
            if (!isRunning() || hasPending()) {
                return null
            }
        }
        val names = known.associateBy { "gs-" + it.pc }
        for ((name, address) in discovery.find(names.keys, timing.discoverMillis)) {
            val pairing = names[name] ?: continue
            if (address.host == pairing.host && address.port == pairing.port) {
                continue
            }
            attempt(pairing, address.host, address.port)?.let { return it }
        }
        return null
    }

    private fun attempt(pairing: Pairing, host: String, port: Int): LanConnection? {
        val joined = join(host, port, "", pairing.keyBytes, pairing.pcBytes) ?: return null
        pairings.remember(pairing.copy(host = host, port = port))
        return joined.connection
    }

    private fun join(host: String, port: Int, token: String, key: ByteArray, expectedPc: ByteArray?): Joined? {
        if (key.size != LanCodec.KEY_SIZE) {
            return null
        }
        val socket = Socket()
        try {
            socket.connect(InetSocketAddress(host, port), timing.connectMillis)
            socket.soTimeout = timing.handshakeMillis
            socket.tcpNoDelay = true
            val nonce = ByteArray(LanCodec.NONCE_SIZE).also { random.nextBytes(it) }
            val hello = LanCodec.hello(token, pairings.phone, nonce)
            val output = socket.getOutputStream()
            output.write(hello)
            output.flush()
            val answer = ByteArray(LanCodec.REPLY_SIZE)
            DataInputStream(socket.getInputStream()).readFully(answer)
            val reply = LanCodec.parseReply(answer)
            if (reply.status != LanStatus.ACCEPTED || (expectedPc != null && !reply.pc.contentEquals(expectedPc))) {
                socket.close()
                return null
            }
            socket.soTimeout = 0
            return Joined(LanConnection(socket, LanCodec.sessionKey(key, hello, answer)), reply.pc)
        } catch (e: IOException) {
            runCatching { socket.close() }
            return null
        } catch (e: IllegalArgumentException) {
            runCatching { socket.close() }
            return null
        }
    }

    private fun isRunning(): Boolean = lock.withLock { running }

    private fun hasPending(): Boolean = lock.withLock { pending != null }

    private fun takePending(): QrLink? = lock.withLock {
        nudged = false
        pending.also { pending = null }
    }

    private fun pause(millis: Long) {
        lock.withLock {
            if (running && !nudged && pending == null) {
                try {
                    changed.await(millis, TimeUnit.MILLISECONDS)
                } catch (e: InterruptedException) {
                    Thread.currentThread().interrupt()
                    running = false
                }
            }
            nudged = false
        }
    }
}

class NsdDiscovery(private val context: Context) : Discovery {
    private val nsd: NsdManager? = context.getSystemService(NsdManager::class.java)

    override fun find(names: Set<String>, timeoutMillis: Long): Map<String, LanAddress> {
        val manager = nsd ?: return emptyMap()
        if (names.isEmpty()) {
            return emptyMap()
        }
        val found = ConcurrentHashMap<String, LanAddress>()
        val done = CountDownLatch(1)
        val record = { name: String, address: LanAddress ->
            found[name] = address
            if (found.keys.containsAll(names)) {
                done.countDown()
            }
        }
        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) = Unit

            override fun onDiscoveryStopped(serviceType: String) = Unit

            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                done.countDown()
            }

            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) = Unit

            override fun onServiceLost(serviceInfo: NsdServiceInfo) = Unit

            override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                val name = serviceInfo.serviceName
                if (name in names && !found.containsKey(name)) {
                    resolve(manager, serviceInfo) { address -> record(name, address) }
                }
            }
        }
        try {
            manager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener)
        } catch (e: IllegalArgumentException) {
            return emptyMap()
        }
        try {
            done.await(timeoutMillis, TimeUnit.MILLISECONDS)
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
        }
        runCatching { manager.stopServiceDiscovery(listener) }
        return HashMap(found)
    }

    private fun resolve(manager: NsdManager, info: NsdServiceInfo, resolved: (LanAddress) -> Unit) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            follow(manager, info, resolved)
        } else {
            resolveOld(manager, info, resolved)
        }
    }

    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun follow(manager: NsdManager, info: NsdServiceInfo, resolved: (LanAddress) -> Unit) {
        val callback = object : NsdManager.ServiceInfoCallback {
            override fun onServiceInfoCallbackRegistrationFailed(errorCode: Int) = Unit

            override fun onServiceUpdated(serviceInfo: NsdServiceInfo) {
                val addresses = serviceInfo.hostAddresses
                val host = addresses.firstOrNull { it is Inet4Address } ?: addresses.firstOrNull()
                val text = host?.hostAddress
                if (text != null) {
                    resolved(LanAddress(text, serviceInfo.port))
                    runCatching { manager.unregisterServiceInfoCallback(this) }
                }
            }

            override fun onServiceLost() = Unit

            override fun onServiceInfoCallbackUnregistered() = Unit
        }
        try {
            manager.registerServiceInfoCallback(info, context.mainExecutor, callback)
        } catch (e: IllegalArgumentException) {
            return
        }
    }

    @Suppress("DEPRECATION")
    private fun resolveOld(manager: NsdManager, info: NsdServiceInfo, resolved: (LanAddress) -> Unit) {
        manager.resolveService(info, object : NsdManager.ResolveListener {
            override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) = Unit

            override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                val text = serviceInfo.host?.hostAddress ?: return
                resolved(LanAddress(text, serviceInfo.port))
            }
        })
    }

    private companion object {
        const val SERVICE_TYPE = "_gs-companion._tcp"
    }
}
