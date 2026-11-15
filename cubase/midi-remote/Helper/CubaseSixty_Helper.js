// Cubase-Sixty – helper MIDI Remote script (no hardware; talks only to the Cubase-Sixty Bridge)
//
// Holds the large hidden channel lists the UF8 script needs, so that the UF8's bank moves do not pay for
// them. Cubase re-checks every channel of a script whenever that script's bank moves: with these lists in
// the UF8 script a bank press took about 0.3 s, without them about 0.07 s (traced). A separate device is not
// re-checked when the UF8 banks.
//
//   - All mixer channels (inputs and outputs excluded, the same types as the UF8 bank), up to 256: names, for
//     follow-selection (SysEx 18 <index hi> <index lo> <name>).
//   - The channels that can record (groups, FX, VCA, inputs and outputs excluded), up to 128: names, for the
//     UF8's record-enable keys (SysEx 19 <slot> <name>).
//   - Which of the first list's channels is selected, and track steps (SysEx 5C <1 forward / 0 back>, from the
//     bridge): the next / previous channel of the list is selected directly (its mValue.mSelected); from a track
//     that is not in the list (a folder, the Stereo Out), Cubase's own next / previous track first. Cubase's own
//     next / previous track skips the tracks in collapsed folders and stops on folders; this does neither, and
//     hidden channels are not in the list (the zone follows the MixConsole's visibility).
//   - Whether any of the first list's channels is soloed (SysEx 1D <0 / 1>), for the UF1's SOLO ACTIVE: Cubase's
//     MIDI Remote has no project-wide solo state.
//
// Names go out when they change. The bridge passes them on to the UF8 script. When the UF8 script asks
// (SysEx 1B, passed on by the bridge; it asks until names arrive, as Cubase drops what is sent while a
// project loads), everything is sent again. Names are encoded 3 x 7 bits per character (UTF-16), so any
// character arrives exactly as Cubase wrote it.
//
// Install in
//   Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\Helper\
// (tools\install-scripts.ps1 does this for you.)

var midiremote_api = require('midiremote_api_v1')

// Log lines also go to the bridge (SysEx 7E <source>), which writes them to cubase\logs\scripts.log, so they can
// be read without Cubase's script console. They are queued and sent from the idle callback (sending needs the
// active device).
var LOG_SOURCE = 4
var logQueue = []
function out(text) {
	console.log(text)
	if (logQueue.length < 1000) logQueue.push(String(text))
}
// At most 20 lines per idle tick (~10 a second), so a long parameter list does not arrive as one big burst.
function flushLog(activeDevice) {
	for (var sent = 0; logQueue.length && sent < 20; ++sent) {
		var t = logQueue.shift()
		var body = [0xF0, 0x7D, 0x52, 0x36, 0x7E, LOG_SOURCE]
		for (var n = 0; n < t.length && n < 400; ++n) {
			var c = t.charCodeAt(n)
			body.push((c >> 14) & 0x7F, (c >> 7) & 0x7F, c & 0x7F)
		}
		body.push(0xF7)
		sendMidi(activeDevice, body)
	}
}

var SYSEX_HEADER = [0xF0, 0x7D, 0x52, 0x36]
var CHANNELS = 256
var REC_CHANNELS = 128
var SCRIPT_VERSION = '0.6.0-dev'
out('[CubaseSixty Helper] script loaded, version ' + SCRIPT_VERSION)

var deviceDriver = midiremote_api.makeDeviceDriver('CubaseSixty', 'Helper', 'Riegus')
var midiInput = deviceDriver.mPorts.makeMidiInput()
var midiOutput = deviceDriver.mPorts.makeMidiOutput()

// ---------------------------------------------------------------------------------------------------------------
// A new bridge (SysEx 7F 01 from it, 14.11.2026): the bridge was restarted while the tray app kept the ports, so
// Cubase did not activate this script again and sends nothing until something changes. The script sends again the
// last of each kind of state it sent (RESEND: command -> how many index bytes follow it), in the order sent. Requests,
// events, temporary lines and log lines are not in the list and are never repeated. (Cubase's own values, the fader
// positions and LEDs, the tray app gives the new bridge.)
// ---------------------------------------------------------------------------------------------------------------
var RESEND = { 0x1A: 2, 0x1D: 0, 0x1F: 3, 0x2F: 1 }
var resendLast = {}, resendSeq = 0
function sendMidi(activeDevice, bytes) {
	if (bytes.length >= 6 && bytes[0] === 0xF0 && RESEND[bytes[4]] !== undefined) {
		var key = bytes.slice(4, 5 + RESEND[bytes[4]]).join(',')
		resendLast[key] = { seq: ++resendSeq, bytes: bytes.slice() }
	}
	midiOutput.sendMidi(activeDevice, bytes)
}
function resendAll(activeDevice) {
	var list = []
	for (var k in resendLast) list.push(resendLast[k])
	list.sort(function (a, b) { return a.seq - b.seq })
	for (var i = 0; i < list.length; ++i) midiOutput.sendMidi(activeDevice, list[i].bytes)
	return list.length
}
deviceDriver
	.makeDetectionUnit()
	.detectPortPair(midiInput, midiOutput)
	.expectInputNameEquals('Cubase-Sixty Helper to Cubase')
	.expectOutputNameEquals('Cubase-Sixty Helper from Cubase')

var surface = deviceDriver.mSurface
// The device needs something to show (nothing to operate). Cubase draws every device at the same height and keeps
// its proportions, so a 6 x 1 label took a third of the MIDI Remote tab: a small square panel instead.
try { surface.makeBlindPanel(0, 0, 1, 1) } catch (e) { surface.makeLabelField(0, 0, 1, 1) }
var page = deviceDriver.mMapping.makePage('Helper')

var allTitles = []
var recTitles = []

function encodeName(text) {
	var out = []
	var str = text || ''
	for (var n = 0; n < str.length && n < 48; ++n) {
		var c = str.charCodeAt(n)
		out.push((c >> 14) & 0x7F, (c >> 7) & 0x7F, c & 0x7F)
	}
	return out
}
function sendAll(activeDevice, index) {
	sendMidi(activeDevice, SYSEX_HEADER.concat([0x18, (index >> 7) & 0x7F, index & 0x7F], encodeName(allTitles[index]), [0xF7]))
}
function sendRec(activeDevice, slot) {
	sendMidi(activeDevice, SYSEX_HEADER.concat([0x19, slot & 0x7F], encodeName(recTitles[slot]), [0xF7]))
}

var soloed = {}, soloCount = 0, soloSent = -1
function sendSolo(activeDevice) {
	var any = soloCount > 0 ? 1 : 0
	if (any === soloSent) return
	soloSent = any
	sendMidi(activeDevice, SYSEX_HEADER.concat([0x1D, any, 0xF7]))
}
// Track steps: which channel is selected (-1: none of the list, e.g. a folder or the Stereo Out), the last one that
// was, and the last step's target (a quick next press counts from it: Cubase reports the selection late).
var selVars = []
var selSeen = []   // (test, 15.11.2026)
var hostMapping = null   // for Cubase's next / previous track (from the callbacks that pass it)
page.mOnActivate = function (activeDevice, activeMapping) { hostMapping = activeMapping }
var selIndex = -1
var stepTarget = -1, stepAt = 0
function stepSelect(activeDevice, dir) {
	var now = Date.now()
	var n = 0
	for (var i = 0; i < CHANNELS; ++i) if (allTitles[i]) n = i + 1
	var recent = stepTarget >= 0 && now - stepAt < 1500
	// The selection is off the list (a folder, the Stereo Out, nothing): Cubase's own next / previous track moves on
	// from where it is (the last channel selected can be far from it, and selecting a channel left the folder
	// selected too, 10.11.2026). Once on a channel, the steps select directly again.
	if (!recent && selIndex < 0) {
		if (!hostMapping) { out('[CubaseSixty Helper] step: no selected channel known yet'); return }
		var action = page.mHostAccess.mTrackSelection.mAction
		;(dir > 0 ? action.mNextTrack : action.mPrevTrack).trigger(hostMapping)
		stepTarget = -1
		out('[CubaseSixty Helper] step ' + (dir > 0 ? '+' : '-') + ': not on a channel of the list; Cubase\'s ' + (dir > 0 ? 'next' : 'previous') + ' track')
		return
	}
	var from = recent ? stepTarget : selIndex
	if (n === 0) return
	var to = Math.max(0, Math.min(n - 1, from + dir))
	if (to === from) return   // the first / last channel
	selVars[to].setProcessValue(activeDevice, 1)
	stepTarget = to
	stepAt = now
	out('[CubaseSixty Helper] step ' + (dir > 0 ? '+' : '-') + ': ' + from + ' -> ' + to + ' "' + allTitles[to] + '"')
}

var allZone = page.mHostAccess.mMixConsole.makeMixerBankZone('Helper: all channels').excludeInputChannels().excludeOutputChannels()
for (var c = 0; c < CHANNELS; ++c) {
	var ch = allZone.makeMixerBankChannel()
	;(function (index, ch) {
		var sel = surface.makeCustomValueVariable('sel' + index)
		page.makeValueBinding(sel, ch.mValue.mSelected)
		sel.mOnProcessValueChange = function (activeDevice, value) {
			if ((value > 0.5) !== !!selSeen[index]) out('[CubaseSixty Helper] sel value: #' + index + ' "' + (allTitles[index] || '') + '" ' + (value > 0.5 ? 'on' : 'off') + scanState())
			selSeen[index] = value > 0.5
			if (value > 0.5) selIndex = index
			else if (selIndex === index) selIndex = -1
		}
		selVars[index] = sel
		var solo = surface.makeCustomValueVariable('solo' + index)
		page.makeValueBinding(solo, ch.mValue.mSolo)
		solo.mOnProcessValueChange = function (activeDevice, value) {
			var on = value > 0.5
			if (!!soloed[index] === on) return
			soloed[index] = on
			soloCount += on ? 1 : -1
			sendSolo(activeDevice)
		}
		ch.mOnTitleChange = function (activeDevice, activeMapping, title) {
			hostMapping = activeMapping
			if (allTitles[index] === title) return
			allTitles[index] = title
			sendAll(activeDevice, index)
			numbersDirty = true
			scanSoon(1000)   // the view lists again, once the full list is still
		}
	})(c, ch)
}

// ---------------------------------------------------------------------------------------------------------------
// Track numbers for the UF8's strips (15.11.2026): each channel of the list above (the MixConsole's visible channels,
// the UF8 bank's order) gets its number as the MixConsole counts it, hidden channels too, the way the UF1 numbers the
// selected track (its 'Track numbers' zone): SysEx 1A <index hi> <index lo> <number hi> <number lo>, 0 = not known.
// Names only in this zone (value bindings in a hidden bank made Cubase pause). The visible list is the full list
// without the hidden channels, in the same order, so it is matched name by name from the top; with several channels
// of one name and some of them hidden, a number can be off.
// ---------------------------------------------------------------------------------------------------------------
var numTitles = []
var sentNumbers = []
var numbersDirty = false
var numZone = page.mHostAccess.mMixConsole.makeMixerBankZone('Helper: numbers').excludeInputChannels().excludeOutputChannels()
	.setFollowVisibility(false)
for (var nc = 0; nc < CHANNELS; ++nc) {
	;(function (index, ch) {
		ch.mOnTitleChange = function (activeDevice, activeMapping, title) {
			if (numTitles[index] === title) return
			numTitles[index] = title
			numbersDirty = true
		}
	})(nc, numZone.makeMixerBankChannel())
}
function sendNumbers(activeDevice) {
	numbersDirty = false
	var j = 0
	for (var i = 0; i < CHANNELS; ++i) {
		var n = 0
		if (allTitles[i]) {
			var k = j
			while (k < CHANNELS && numTitles[k] !== allTitles[i]) ++k
			if (k < CHANNELS) { n = k + 1; j = k + 1 }
		}
		if (sentNumbers[i] === n) continue
		if (sentNumbers[i] === undefined && n === 0) { sentNumbers[i] = 0; continue }   // nothing to say yet
		sentNumbers[i] = n
		sendMidi(activeDevice, SYSEX_HEADER.concat([0x1A, (i >> 7) & 0x7F, i & 0x7F, (n >> 7) & 0x7F, n & 0x7F, 0xF7]))
	}
}

// ---------------------------------------------------------------------------------------------------------------
// The UF8's track views (docs/track-views.md, step 5): each filtered view's channels, in the order of its bank (the
// same filters as the UF8 script's views, following visibility as its bank does). Names only: SysEx 1E <view>
// <index hi> <index lo> <name> to the UF8 script (follow selection, the bank position in a view), and each one's
// track number as 1F <view> <index hi> <index lo> <number hi> <number lo> to the bridge (the strips' numbers), matched
// against the numbers zone as the full list is. The bridge also counts them (the view picker dims empty views).
// Read through a window of 16 channels per view that the script banks through itself (its bank actions triggered,
// which Cubase carries out at once), not a zone as long as the list: six zones of 128-256 channels made Cubase stall
// about 0.9 s longer after every selection change (measured 15.11.2026: longest gap 2.0 s instead of 1.1 s). The lists
// are read 1 s after the full list last changed (a track added, removed, renamed, hidden), all six one after another.
// ---------------------------------------------------------------------------------------------------------------
var VIEW_LISTS = [
	null,   // 0 = ALL: the full list above
	{ name: 'audio', filters: ['includeAudioChannels'] },
	{ name: 'instruments', filters: ['includeInstrumentChannels', 'includeSamplerChannels'] },
	{ name: 'MIDI', filters: ['includeMIDIChannels'] },
	{ name: 'groups', filters: ['includeGroupChannels'] },
	{ name: 'FX', filters: ['includeFXChannels'] },
	{ name: 'VCA', filters: ['includeVCAChannels'] }
]
var SCAN_WIDTH = 16, VIEW_MAX = 256
var viewTitles = [], sentViewNumbers = []
var viewZones = [], scanSlots = [], scanSlotAt = []
function sendViewTitle(activeDevice, v, index) {
	sendMidi(activeDevice, SYSEX_HEADER.concat([0x1E, v, (index >> 7) & 0x7F, index & 0x7F], encodeName(viewTitles[v][index]), [0xF7]))
}
for (var vl = 1; vl < VIEW_LISTS.length; ++vl) {
	viewTitles[vl] = []
	sentViewNumbers[vl] = []
	scanSlots[vl] = []
	scanSlotAt[vl] = 0
	var vz = page.mHostAccess.mMixConsole.makeMixerBankZone('Helper: ' + VIEW_LISTS[vl].name)
	for (var vf = 0; vf < VIEW_LISTS[vl].filters.length; ++vf) {
		try { vz[VIEW_LISTS[vl].filters[vf]]() } catch (e) { out('[CubaseSixty Helper] ' + VIEW_LISTS[vl].name + ': ' + VIEW_LISTS[vl].filters[vf] + ' not available: ' + e) }
	}
	viewZones[vl] = vz
	for (var vc = 0; vc < SCAN_WIDTH; ++vc) {
		;(function (v, slot, ch) {
			ch.mOnTitleChange = function (activeDevice, activeMapping, title) {
				hostMapping = activeMapping
				scanSlots[v][slot] = title || ''
				scanSlotAt[v] = Date.now()
			}
		})(vl, vc, vz.makeMixerBankChannel())
	}
}
// The reading: reset the window, take its 16 names, next bank, ... until a bank is not full. A step is read once the
// window has settled: 150 ms without a new name after one came, or 400 ms after the move if none came (the same names).
var scan = { v: 0, bank: 0, found: [], movedAt: 0, queue: [], dueAt: 0, started: 0 }
function scanSoon(delayMs) { scan.dueAt = Date.now() + delayMs }
// (Test, 15.11.2026: the selection moved by itself while the lists were read at a project's start; when, against the
// reading's moves.)
var helperStart = Date.now()
function scanState() {
	var now = Date.now()
	return ' at +' + (now - helperStart) + ' ms' + (scan.v ? ' (reading ' + VIEW_LISTS[scan.v].name + ', bank ' + scan.bank + ', moved ' + (now - scan.movedAt) + ' ms ago)' : ' (not reading)')
}
function scanMove(kind) {
	if (!hostMapping) return false
	var a = viewZones[scan.v].mAction
	try { (kind === 'reset' ? a.mResetBank : a.mNextBank).trigger(hostMapping) } catch (e) { out('[CubaseSixty Helper] view lists: ' + e); return false }
	scan.movedAt = Date.now()
	out('[CubaseSixty Helper] reading ' + VIEW_LISTS[scan.v].name + ': ' + kind + ' at +' + (scan.movedAt - helperStart) + ' ms')
	return true
}
function scanNextView(activeDevice) {
	scan.v = scan.queue.length ? scan.queue.shift() : 0
	if (!scan.v) { out('[CubaseSixty Helper] view lists read in ' + (Date.now() - scan.started) + ' ms'); return }
	scan.bank = 0
	scan.found = []
	if (!scanMove('reset')) { scan.v = 0; scan.queue = []; scanSoon(1000) }
}
function scanStep(activeDevice) {
	var v = scan.v, now = Date.now()
	var settled = scanSlotAt[v] > scan.movedAt ? now - scanSlotAt[v] >= 150 : now - scan.movedAt >= 400
	if (!settled) return
	var base = scan.bank * SCAN_WIDTH, full = true
	for (var k = 0; k < SCAN_WIDTH; ++k) {
		var t = scanSlots[v][k] || ''
		scan.found[base + k] = t
		if (!t) full = false
	}
	var shown = scanSlots[v].filter(function (x) { return !!x })
	var selT = selIndex >= 0 ? allTitles[selIndex] : ''
	out('[CubaseSixty Helper] reading ' + VIEW_LISTS[v].name + ' bank ' + scan.bank + ': ' + shown.length + ' names, "' + (shown[0] || '') + '" .. "' + (shown[shown.length - 1] || '') + '"' + (selT && shown.indexOf(selT) >= 0 ? ', the selected track among them' : ''))
	if (full && base + SCAN_WIDTH < VIEW_MAX) {
		++scan.bank
		if (scanMove('next')) return
	}
	// The view read: what changed goes out (removed ones as empty names), and its numbers.
	var list = viewTitles[v], found = scan.found
	var n = Math.max(list.length, found.length)
	for (var i = 0; i < n && i < VIEW_MAX; ++i) {
		var f = found[i] || ''
		if ((list[i] || '') === f && list[i] !== undefined) continue
		list[i] = f
		sendViewTitle(activeDevice, v, i)
	}
	numbersDirty = true
	scanNextView(activeDevice)
}
function scanIdle(activeDevice) {
	if (scan.v) { scanStep(activeDevice); return }
	if (!scan.dueAt || Date.now() < scan.dueAt) return
	scan.dueAt = 0
	scan.queue = [1, 2, 3, 4, 5, 6]
	scan.started = Date.now()
	scanNextView(activeDevice)
}
// A list's numbers: matched name by name, in order, against the numbers zone (as sendNumbers does the full list).
function sendViewNumbers(activeDevice, v) {
	var list = viewTitles[v], sent = sentViewNumbers[v], j = 0
	for (var i = 0; i < VIEW_MAX; ++i) {
		var n = 0
		if (list[i]) {
			var k = j
			while (k < CHANNELS && numTitles[k] !== list[i]) ++k
			if (k < CHANNELS) { n = k + 1; j = k + 1 }
		}
		if (sent[i] === n) continue
		if (sent[i] === undefined && n === 0) { sent[i] = 0; continue }
		sent[i] = n
		sendMidi(activeDevice, SYSEX_HEADER.concat([0x1F, v, (i >> 7) & 0x7F, i & 0x7F, (n >> 7) & 0x7F, n & 0x7F, 0xF7]))
	}
}

var recZone = page.mHostAccess.mMixConsole.makeMixerBankZone('Helper: recordable')
// Includes, the same as the UF8's ARMED strip zones, so the positions match (15.11.2026: "exclude inputs, outputs,
// groups, FX, VCA" held one channel more, not identified; every instrument track checked was in the includes list).
var recFilters = ['includeAudioChannels', 'includeInstrumentChannels', 'includeSamplerChannels', 'includeMIDIChannels']
for (var f = 0; f < recFilters.length; ++f) {
	try { recZone[recFilters[f]]() } catch (e) { out('[CubaseSixty Helper] recordable list: ' + recFilters[f] + ' not available: ' + e) }
}
// Each recordable channel's record-enable (the UF8's ARMED view, docs/track-views.md step 6): SysEx 2F <slot> <0 / 1>
// to the UF8 script, on change. (A value binding per slot: record-enable changes seldom, unlike the values that made
// hidden banks with bindings pause Cubase.)
var recArmed = []
function sendArmed(activeDevice, slot) {
	sendMidi(activeDevice, SYSEX_HEADER.concat([0x2F, slot & 0x7F, recArmed[slot] ? 1 : 0, 0xF7]))
}
for (var k = 0; k < REC_CHANNELS; ++k) {
	var recChannel = recZone.makeMixerBankChannel()
	;(function (slot, ch) {
		ch.mOnTitleChange = function (activeDevice, activeMapping, title) {
			if (recTitles[slot] === title) return
			recTitles[slot] = title
			sendRec(activeDevice, slot)
		}
		var arm = surface.makeCustomValueVariable('recArm' + slot)
		page.makeValueBinding(arm, ch.mValue.mRecordEnable)
		arm.mOnProcessValueChange = function (activeDevice, value) {
			var on = value > 0.5
			if (recArmed[slot] === on) return
			recArmed[slot] = on
			sendArmed(activeDevice, slot)
		}
	})(k, recChannel)
}

// The UF8 script asks for everything (SysEx 1B): send both lists, empty slots included.
var resends = 0
midiInput.mOnSysex = function (activeDevice, message) {
	if (message.length >= 7 && message[4] === 0x7F && message[5] === 0x01) { out('[CubaseSixty Helper] a new bridge: ' + resendAll(activeDevice) + ' messages sent again'); return }
	if (message.length >= 7 && message[4] === 0x5C) {
		try { stepSelect(activeDevice, message[5] ? 1 : -1) } catch (e) { out('[CubaseSixty Helper] step: ' + e) }
		return
	}
	if (message.length < 6 || message[4] !== 0x1B) return
	if (++resends <= 3 || resends % 20 === 0) out('[CubaseSixty Helper] lists requested (' + resends + ')')
	for (var i = 0; i < CHANNELS; ++i) sendAll(activeDevice, i)
	for (var s = 0; s < REC_CHANNELS; ++s) { sendRec(activeDevice, s); if (recArmed[s]) sendArmed(activeDevice, s) }
	// The views' lists up to their last name, and one empty entry after it (a list that shrank).
	for (var v = 1; v < VIEW_LISTS.length; ++v) {
		var last = -1
		for (var vi = 0; vi < VIEW_MAX; ++vi) if (viewTitles[v][vi]) last = vi
		for (var vj = 0; vj <= last + 1 && vj < VIEW_MAX; ++vj) sendViewTitle(activeDevice, v, vj)
	}
	soloSent = -1
	sendSolo(activeDevice)
}

// Closing a project (Cubase shows the Hub) deactivates the scripts' drivers, opening one activates them again: logged.
deviceDriver.mOnActivate = function (activeDevice) { helperStart = Date.now(); out('[CubaseSixty Helper] active (a project is open)') }
deviceDriver.mOnDeactivate = function (activeDevice) {
	out('[CubaseSixty Helper] inactive (no project)')
	resendLast = {}
	sentNumbers = []
	for (var v = 1; v < VIEW_LISTS.length; ++v) sentViewNumbers[v] = []
	scan.v = 0; scan.queue = []; scan.dueAt = 0
	flushLog(activeDevice)
}
deviceDriver.mOnIdle = function (activeDevice) {
	try { scanIdle(activeDevice) } catch (e) { out('[CubaseSixty Helper] view lists: ' + e); scan.v = 0 }
	if (numbersDirty) {   // once the names have come in (they arrive in a burst)
		sendNumbers(activeDevice)
		for (var v = 1; v < VIEW_LISTS.length; ++v) sendViewNumbers(activeDevice, v)
	}
	flushLog(activeDevice)
}
