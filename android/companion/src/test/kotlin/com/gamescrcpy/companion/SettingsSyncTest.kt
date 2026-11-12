package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Enums.CompanionItem
import com.gamescrcpy.protocol.Enums.ComputerLink
import com.gamescrcpy.protocol.Messages
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.DisplayName
import org.junit.jupiter.api.Test

class SettingsSyncTest {
    private class MemoryStore : SyncStore {
        val values = HashMap<String, String?>()

        override fun read(key: String): String? = values[key]

        override fun write(key: String, value: String?) {
            values[key] = value
        }
    }

    private var now = 1_000L
    private val store = MemoryStore()

    private fun sync() = SettingsSync(store) { now }

    private fun snapshot(
        id: Int,
        applied: Long = 0,
        values: SyncValues = SyncValues(),
        name: String = "PC-$id",
        link: ComputerLink = ComputerLink.USB,
    ) = Messages.CompanionSettings().also {
        it.computer = ByteArray(16) { id.toByte() }
        it.name = name
        it.link = link
        it.accent = 0x22D3C5
        it.applied = applied
        it.cursor = values.cursor
        it.hints = values.hints
        it.hintOpacity = values.hintOpacity
        it.ime = values.ime
        it.autoConnect = values.autoConnect
    }

    private fun idOf(id: Int) = Hex.encode(ByteArray(16) { id.toByte() })

    @Test
    @DisplayName("ADR-0112: a snapshot is kept per computer and its values show on the switches")
    fun aSnapshotIsKeptPerComputerAndShown() {
        val sync = sync()
        assertEquals(SyncValues(), sync.values)
        assertNull(sync.connected)
        val link = sync.open("") { }
        sync.snapshot(link, snapshot(1, values = SyncValues(cursor = true, hintOpacity = 35), name = "DESKTOP-1"))
        assertEquals(SyncValues(cursor = true, hintOpacity = 35), sync.values)
        val computer = sync.connected
        assertEquals(idOf(1), computer?.id)
        assertEquals("DESKTOP-1", computer?.name)
        assertEquals(0x22D3C5, computer?.accent)
        assertEquals(1_000L, computer?.lastSeen)
        sync.close(link)
        assertNull(sync.connected)
        assertEquals(idOf(1), sync.latest?.id)
        assertEquals(SyncValues(cursor = true, hintOpacity = 35), sync.values)

        now = 2_000L
        val other = sync.open("") { }
        sync.snapshot(other, snapshot(2, values = SyncValues(ime = false), link = ComputerLink.WIRELESS))
        sync.close(other)
        assertEquals(listOf(idOf(2), idOf(1)), sync.known.map { it.id })
        assertEquals(SyncValues(ime = false), sync.values)
    }

    @Test
    @DisplayName("ADR-0112: a change goes to the connected computer with its number and shows until the computer answers")
    fun aChangeIsSentAndKeptUntilAnswered() {
        val sync = sync()
        val sent = ArrayList<Messages.CompanionSettingChange>()
        val link = sync.open("") { sent += it }
        sync.change(CompanionItem.CURSOR, 1)
        assertTrue(sent.isEmpty())
        sync.snapshot(link, snapshot(1))
        assertEquals(1, sent.size)
        assertEquals(1L, sent[0].change)
        assertEquals(CompanionItem.CURSOR, sent[0].item)
        assertEquals(1, sent[0].value)

        sync.change(CompanionItem.HINT_OPACITY, 40)
        assertEquals(2L, sent[1].change)
        assertEquals(CompanionItem.HINT_OPACITY, sent[1].item)
        assertEquals(40, sent[1].value)
        sync.snapshot(link, snapshot(1, applied = 1, values = SyncValues(cursor = true)))
        assertEquals(SyncValues(cursor = true, hintOpacity = 40), sync.values)
        assertEquals(setOf(CompanionItem.HINT_OPACITY), sync.pendingItems)
        sync.snapshot(link, snapshot(1, applied = 2, values = SyncValues(cursor = true, hintOpacity = 40)))
        assertTrue(sync.pendingItems.isEmpty())
        assertEquals(2, sent.size)

        sync.change(CompanionItem.CURSOR, 1)
        assertEquals(2, sent.size)
        sync.change(CompanionItem.HINT_OPACITY, 7)
        assertEquals(2, sent.size)
    }

    @Test
    @DisplayName("ADR-0112: when both sides change, the change that reaches the computer later wins")
    fun theLaterChangeAtTheComputerWins() {
        val sync = sync()
        val sent = ArrayList<Messages.CompanionSettingChange>()
        val link = sync.open("") { sent += it }
        sync.snapshot(link, snapshot(1))
        sync.change(CompanionItem.HINTS, 1)
        sync.snapshot(link, snapshot(1, applied = 0, values = SyncValues(ime = false)))
        assertEquals(SyncValues(hints = true, ime = false), sync.values)
        sync.snapshot(link, snapshot(1, applied = 1, values = SyncValues(hints = true, ime = false)))
        assertEquals(SyncValues(hints = true, ime = false), sync.values)
        sync.snapshot(link, snapshot(1, applied = 1, values = SyncValues(hints = false, ime = false)))
        assertEquals(SyncValues(hints = false, ime = false), sync.values)
        assertTrue(sync.pendingItems.isEmpty())
    }

    @Test
    @DisplayName("ADR-0112: changes made while not connected survive a restart and go out after the first snapshot")
    fun offlineChangesSurviveAndAreSentLater() {
        val first = sync()
        first.change(CompanionItem.HINTS, 1)
        first.change(CompanionItem.AUTO_CONNECT, 0)
        first.change(CompanionItem.IME, 0)
        assertEquals(SyncValues(hints = true, ime = false, autoConnect = false), first.values)

        val again = sync()
        assertEquals(setOf(CompanionItem.HINTS, CompanionItem.AUTO_CONNECT, CompanionItem.IME), again.pendingItems)
        assertFalse(again.values.autoConnect)
        val sent = ArrayList<Messages.CompanionSettingChange>()
        val link = again.open("") { sent += it }
        assertTrue(sent.isEmpty())
        again.snapshot(link, snapshot(3, values = SyncValues(ime = false)))
        assertEquals(listOf(CompanionItem.HINTS, CompanionItem.AUTO_CONNECT), sent.map { it.item })
        assertEquals(listOf(1L, 2L), sent.map { it.change })
        assertEquals(setOf(CompanionItem.HINTS, CompanionItem.AUTO_CONNECT), again.pendingItems)
        assertEquals(SyncValues(hints = true, ime = false, autoConnect = false), again.values)
    }

    @Test
    @DisplayName("ADR-0112: a change that the computer did not answer before the connection ended is sent again")
    fun anUnansweredChangeIsSentAgain() {
        val sync = sync()
        val first = ArrayList<Messages.CompanionSettingChange>()
        val link = sync.open("") { first += it }
        sync.snapshot(link, snapshot(1))
        sync.change(CompanionItem.CURSOR, 1)
        assertEquals(1, first.size)
        sync.close(link)
        assertTrue(sync.values.cursor)
        sync.change(CompanionItem.IME, 0)
        assertEquals(1, first.size)

        val second = ArrayList<Messages.CompanionSettingChange>()
        val next = sync.open("") { second += it }
        sync.snapshot(next, snapshot(1))
        assertEquals(listOf(CompanionItem.CURSOR, CompanionItem.IME), second.map { it.item })
        assertEquals(listOf(1L, 2L), second.map { it.change })
        sync.snapshot(next, snapshot(1, applied = 2, values = SyncValues(cursor = true, ime = false)))
        assertTrue(sync.pendingItems.isEmpty())
    }

    @Test
    @DisplayName("ADR-0112: a snapshot after the link closed changes nothing, and values outside the range are refused")
    fun lateSnapshotsAndBadValuesAreIgnored() {
        val sync = sync()
        val link = sync.open("") { }
        sync.close(link)
        sync.snapshot(link, snapshot(1, values = SyncValues(cursor = true)))
        assertTrue(sync.known.isEmpty())
        sync.change(CompanionItem.HINT_OPACITY, 9)
        sync.change(CompanionItem.HINT_OPACITY, 101)
        sync.change(CompanionItem.CURSOR, 2)
        assertTrue(sync.pendingItems.isEmpty())
        assertEquals(SyncValues(), sync.values)
    }

    @Test
    @DisplayName("ADR-0112: forgetting a computer drops its record and the switches show the latest other one")
    fun forgettingAComputerFallsBackToTheLatestOther() {
        val sync = sync()
        val first = sync.open("") { }
        sync.snapshot(first, snapshot(1, values = SyncValues(cursor = true)))
        sync.close(first)
        now = 5_000L
        val second = sync.open("ab".repeat(16)) { }
        sync.snapshot(second, snapshot(2, values = SyncValues(hints = true), link = ComputerLink.LAN))
        sync.close(second)
        assertEquals(listOf("ab".repeat(16), ""), sync.known.map { it.pc })
        sync.forget(idOf(2))
        assertEquals(listOf(idOf(1)), sync.known.map { it.id })
        assertEquals(SyncValues(cursor = true), sync.values)
        assertEquals(listOf(idOf(1)), sync().known.map { it.id })
    }

    @Test
    @DisplayName("ADR-0112: stored computers and changes survive encoding and damaged lines are skipped")
    fun storedRecordsSurviveAndDamagedLinesAreSkipped() {
        val computer = KnownComputer(
            id = idOf(9),
            name = "游戏电脑, \"home\"",
            link = ComputerLink.LAN,
            accent = 0xFFB020,
            values = SyncValues(cursor = true, hints = true, hintOpacity = 10, ime = false, autoConnect = false),
            lastSeen = 1_731_300_000_000L,
            pc = "cd".repeat(16),
        )
        val text = SyncCodec.encodeComputers(listOf(computer))
        assertEquals(listOf(computer), SyncCodec.decodeComputers(text))
        val good = text.lines().single()
        val fields = good.split(',')
        fun with(index: Int, value: String) = fields.toMutableList().also { it[index] = value }.joinToString(",")
        val damaged = listOf(
            with(0, "zz"),
            with(1, "x"),
            with(2, "9"),
            with(3, "16777216"),
            with(4, "2"),
            with(6, "5"),
            with(9, "soon"),
            with(10, "abc"),
            "$good,extra",
        )
        assertEquals(listOf(computer), SyncCodec.decodeComputers((damaged + good).joinToString("\n")))
        assertTrue(SyncCodec.decodeComputers(null).isEmpty())

        val pending = mapOf(CompanionItem.CURSOR to 1, CompanionItem.HINT_OPACITY to 55)
        assertEquals(pending, SyncCodec.decodePending(SyncCodec.encodePending(pending)))
        assertEquals(mapOf(CompanionItem.IME to 0), SyncCodec.decodePending("3,0\n9,1\n2,5\n0,x\n1"))
    }
}
