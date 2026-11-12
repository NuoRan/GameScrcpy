package com.gamescrcpy.companion

import android.content.Context
import com.gamescrcpy.protocol.Constants
import com.gamescrcpy.protocol.Enums.CompanionItem
import com.gamescrcpy.protocol.Enums.ComputerLink
import com.gamescrcpy.protocol.Messages
import java.nio.charset.StandardCharsets
import java.util.concurrent.CopyOnWriteArrayList

data class SyncValues(
    val cursor: Boolean = false,
    val hints: Boolean = false,
    val hintOpacity: Int = Constants.DEFAULT_PHONE_HINT_OPACITY,
    val ime: Boolean = true,
    val autoConnect: Boolean = true,
) {
    fun valueOf(item: CompanionItem): Int = when (item) {
        CompanionItem.CURSOR -> flag(cursor)
        CompanionItem.HINTS -> flag(hints)
        CompanionItem.HINT_OPACITY -> hintOpacity
        CompanionItem.IME -> flag(ime)
        CompanionItem.AUTO_CONNECT -> flag(autoConnect)
    }

    fun with(item: CompanionItem, value: Int): SyncValues = when (item) {
        CompanionItem.CURSOR -> copy(cursor = value != 0)
        CompanionItem.HINTS -> copy(hints = value != 0)
        CompanionItem.HINT_OPACITY -> copy(hintOpacity = value.coerceIn(MIN_OPACITY, MAX_OPACITY))
        CompanionItem.IME -> copy(ime = value != 0)
        CompanionItem.AUTO_CONNECT -> copy(autoConnect = value != 0)
    }

    companion object {
        const val MIN_OPACITY = Constants.MIN_PHONE_HINT_OPACITY
        const val MAX_OPACITY = Constants.MAX_PHONE_HINT_OPACITY

        fun of(message: Messages.CompanionSettings) = SyncValues(
            cursor = message.cursor,
            hints = message.hints,
            hintOpacity = message.hintOpacity,
            ime = message.ime,
            autoConnect = message.autoConnect,
        )

        fun fits(item: CompanionItem, value: Int): Boolean =
            if (item == CompanionItem.HINT_OPACITY) value in MIN_OPACITY..MAX_OPACITY else value == 0 || value == 1

        private fun flag(on: Boolean): Int = if (on) 1 else 0
    }
}

data class KnownComputer(
    val id: String,
    val name: String,
    val link: ComputerLink,
    val accent: Int,
    val values: SyncValues,
    val lastSeen: Long,
    val pc: String = "",
)

interface SyncStore {
    fun read(key: String): String?

    fun write(key: String, value: String?)
}

class PreferenceSyncStore(context: Context, file: String) : SyncStore {
    private val preferences = context.applicationContext.getSharedPreferences(file, Context.MODE_PRIVATE)

    override fun read(key: String): String? = preferences.getString(key, null)

    override fun write(key: String, value: String?) {
        preferences.edit().putString(key, value).apply()
    }
}

object SyncCodec {
    private const val FIELDS = 11
    private const val ID_LENGTH = Constants.COMPANION_COMPUTER_ID_SIZE * 2

    fun encodeComputers(computers: Collection<KnownComputer>): String = computers.joinToString("\n") {
        listOf(
            it.id,
            Hex.encode(it.name.toByteArray(StandardCharsets.UTF_8)),
            it.link.wireValue().toString(),
            it.accent.toString(),
            bit(it.values.cursor),
            bit(it.values.hints),
            it.values.hintOpacity.toString(),
            bit(it.values.ime),
            bit(it.values.autoConnect),
            it.lastSeen.toString(),
            it.pc,
        ).joinToString(",")
    }

    fun decodeComputers(text: String?): List<KnownComputer> {
        if (text.isNullOrEmpty()) {
            return emptyList()
        }
        return text.split('\n').mapNotNull(::decodeComputer)
    }

    fun encodePending(pending: Map<CompanionItem, Int>): String =
        pending.entries.joinToString("\n") { "${it.key.wireValue()},${it.value}" }

    fun decodePending(text: String?): Map<CompanionItem, Int> {
        if (text.isNullOrEmpty()) {
            return emptyMap()
        }
        val pending = LinkedHashMap<CompanionItem, Int>()
        for (line in text.split('\n')) {
            val fields = line.split(',')
            val item = fields.firstOrNull()?.toIntOrNull()?.let { CompanionItem.CODEC.fromWire(it) } ?: continue
            val value = fields.getOrNull(1)?.toIntOrNull() ?: continue
            if (fields.size == 2 && SyncValues.fits(item, value)) {
                pending[item] = value
            }
        }
        return pending
    }

    private fun decodeComputer(line: String): KnownComputer? {
        val fields = line.split(',')
        if (fields.size != FIELDS || fields[0].length != ID_LENGTH || Hex.decode(fields[0]) == null) {
            return null
        }
        val name = Hex.decode(fields[1])?.toString(StandardCharsets.UTF_8) ?: return null
        val link = fields[2].toIntOrNull()?.let { ComputerLink.CODEC.fromWire(it) } ?: return null
        val accent = fields[3].toIntOrNull()?.takeIf { it in 0..ACCENT_MAX } ?: return null
        val opacity = fields[6].toIntOrNull()?.takeIf { SyncValues.fits(CompanionItem.HINT_OPACITY, it) } ?: return null
        val flags = listOf(fields[4], fields[5], fields[7], fields[8]).map { it.toIntOrNull() }
        if (flags.any { it != 0 && it != 1 }) {
            return null
        }
        val lastSeen = fields[9].toLongOrNull() ?: return null
        val pc = fields[10].takeIf { it.isEmpty() || (it.length == ID_LENGTH && Hex.decode(it) != null) } ?: return null
        val values = SyncValues(
            cursor = flags[0] == 1,
            hints = flags[1] == 1,
            hintOpacity = opacity,
            ime = flags[2] == 1,
            autoConnect = flags[3] == 1,
        )
        return KnownComputer(fields[0], name, link, accent, values, lastSeen, pc)
    }

    private fun bit(on: Boolean): String = if (on) "1" else "0"

    private const val ACCENT_MAX = 0xFFFFFF
}

class SettingsSync(private val store: SyncStore, private val clock: () -> Long) {
    fun interface Listener {
        fun changed()
    }

    class Link internal constructor(internal val pc: String, internal val send: (Messages.CompanionSettingChange) -> Unit) {
        internal var next = 1L
        internal val sent = HashMap<CompanionItem, Long>()
        internal var computer: String? = null

        internal fun changeOf(item: CompanionItem, value: Int): Messages.CompanionSettingChange {
            val number = next++
            sent[item] = number
            return Messages.CompanionSettingChange().also {
                it.change = number
                it.item = item
                it.value = value
            }
        }
    }

    private val lock = Any()
    private val listeners = CopyOnWriteArrayList<Listener>()
    private val computers = LinkedHashMap<String, KnownComputer>()
    private val pending = LinkedHashMap<CompanionItem, Int>()
    private val links = ArrayList<Link>()
    private var last: String? = null

    init {
        SyncCodec.decodeComputers(store.read(COMPUTERS)).forEach { computers[it.id] = it }
        pending.putAll(SyncCodec.decodePending(store.read(PENDING)))
        last = store.read(LAST)?.takeIf { it in computers }
    }

    val values: SyncValues
        get() = synchronized(lock) { shownLocked() }

    val pendingItems: Set<CompanionItem>
        get() = synchronized(lock) { pending.keys.toSet() }

    val known: List<KnownComputer>
        get() = synchronized(lock) { computers.values.sortedByDescending { it.lastSeen } }

    val connected: KnownComputer?
        get() = synchronized(lock) { links.lastOrNull { it.computer != null }?.computer?.let { computers[it] } }

    val latest: KnownComputer?
        get() = synchronized(lock) { last?.let { computers[it] } }

    fun addListener(listener: Listener) {
        listeners.addIfAbsent(listener)
    }

    fun removeListener(listener: Listener) {
        listeners.remove(listener)
    }

    fun change(item: CompanionItem, value: Int) {
        if (!SyncValues.fits(item, value)) {
            return
        }
        val out = ArrayList<Pair<Link, Messages.CompanionSettingChange>>()
        synchronized(lock) {
            if (shownLocked().valueOf(item) == value) {
                return
            }
            pending[item] = value
            for (link in links) {
                if (link.computer != null) {
                    out += link to link.changeOf(item, value)
                }
            }
            saveLocked()
        }
        out.forEach { (link, message) -> link.send(message) }
        changed()
    }

    fun open(pc: String, send: (Messages.CompanionSettingChange) -> Unit): Link {
        val link = Link(pc, send)
        synchronized(lock) { links += link }
        return link
    }

    fun snapshot(link: Link, message: Messages.CompanionSettings) {
        val out = ArrayList<Messages.CompanionSettingChange>()
        synchronized(lock) {
            if (link !in links) {
                return
            }
            val id = Hex.encode(message.computer)
            val values = SyncValues.of(message)
            val earlier = computers[id]
            computers[id] = KnownComputer(
                id = id,
                name = message.name,
                link = message.link,
                accent = (message.accent and ACCENT_MASK).toInt(),
                values = values,
                lastSeen = clock(),
                pc = link.pc.ifEmpty { earlier?.pc.orEmpty() },
            )
            last = id
            val first = link.computer == null
            link.computer = id
            val answered = link.sent.filterValues { message.applied >= it }.keys
            for (item in answered) {
                link.sent.remove(item)
                settleLocked(item, link)
            }
            if (first) {
                for ((item, value) in pending.toList()) {
                    if (item in link.sent) {
                        continue
                    }
                    if (values.valueOf(item) == value) {
                        settleLocked(item, link)
                    } else {
                        out += link.changeOf(item, value)
                    }
                }
            }
            saveLocked()
        }
        out.forEach(link.send)
        changed()
    }

    fun close(link: Link) {
        synchronized(lock) {
            if (!links.remove(link)) {
                return
            }
        }
        changed()
    }

    fun forget(id: String) {
        synchronized(lock) {
            computers.remove(id) ?: return
            if (last == id) {
                last = computers.values.maxByOrNull { it.lastSeen }?.id
            }
            saveLocked()
        }
        changed()
    }

    private fun settleLocked(item: CompanionItem, answering: Link) {
        if (links.none { it !== answering && item in it.sent }) {
            pending.remove(item)
        }
    }

    private fun shownLocked(): SyncValues {
        var shown = last?.let { computers[it]?.values } ?: SyncValues()
        for ((item, value) in pending) {
            shown = shown.with(item, value)
        }
        return shown
    }

    private fun saveLocked() {
        store.write(COMPUTERS, SyncCodec.encodeComputers(computers.values))
        store.write(PENDING, SyncCodec.encodePending(pending))
        store.write(LAST, last)
    }

    private fun changed() {
        listeners.forEach { it.changed() }
    }

    companion object {
        private const val FILE = "sync"
        private const val COMPUTERS = "computers"
        private const val PENDING = "pending"
        private const val LAST = "last"
        private const val ACCENT_MASK = 0xFFFFFFL

        @Volatile
        private var shared: SettingsSync? = null

        fun of(context: Context): SettingsSync = shared ?: synchronized(this) {
            shared ?: SettingsSync(PreferenceSyncStore(context, FILE), System::currentTimeMillis).also { shared = it }
        }
    }
}
