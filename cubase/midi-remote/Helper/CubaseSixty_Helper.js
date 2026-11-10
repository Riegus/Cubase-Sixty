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
		midiOutput.sendMidi(activeDevice, body)
	}
}

var SYSEX_HEADER = [0xF0, 0x7D, 0x52, 0x36]
var CHANNELS = 256
var REC_CHANNELS = 128
var SCRIPT_VERSION = '0.3.1-dev'
out('[CubaseSixty Helper] script loaded, version ' + SCRIPT_VERSION)

var deviceDriver = midiremote_api.makeDeviceDriver('CubaseSixty', 'Helper', 'Riegus')
var midiInput = deviceDriver.mPorts.makeMidiInput()
var midiOutput = deviceDriver.mPorts.makeMidiOutput()
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
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x18, (index >> 7) & 0x7F, index & 0x7F], encodeName(allTitles[index]), [0xF7]))
}
function sendRec(activeDevice, slot) {
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x19, slot & 0x7F], encodeName(recTitles[slot]), [0xF7]))
}

var soloed = {}, soloCount = 0, soloSent = -1
function sendSolo(activeDevice) {
	var any = soloCount > 0 ? 1 : 0
	if (any === soloSent) return
	soloSent = any
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x1D, any, 0xF7]))
}
// Track steps: which channel is selected (-1: none of the list, e.g. a folder or the Stereo Out), the last one that
// was, and the last step's target (a quick next press counts from it: Cubase reports the selection late).
var selVars = []
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
		}
	})(c, ch)
}

var recZone = page.mHostAccess.mMixConsole.makeMixerBankZone('Helper: recordable')
var recFilters = ['excludeInputChannels', 'excludeOutputChannels', 'excludeGroupChannels', 'excludeFXChannels', 'excludeVCAChannels']
for (var f = 0; f < recFilters.length; ++f) {
	try { recZone[recFilters[f]]() } catch (e) { out('[CubaseSixty Helper] recordable list: ' + recFilters[f] + ' not available: ' + e) }
}
for (var k = 0; k < REC_CHANNELS; ++k) {
	var recChannel = recZone.makeMixerBankChannel()
	;(function (slot, ch) {
		ch.mOnTitleChange = function (activeDevice, activeMapping, title) {
			if (recTitles[slot] === title) return
			recTitles[slot] = title
			sendRec(activeDevice, slot)
		}
	})(k, recChannel)
}

// The UF8 script asks for everything (SysEx 1B): send both lists, empty slots included.
var resends = 0
midiInput.mOnSysex = function (activeDevice, message) {
	if (message.length >= 7 && message[4] === 0x5C) {
		try { stepSelect(activeDevice, message[5] ? 1 : -1) } catch (e) { out('[CubaseSixty Helper] step: ' + e) }
		return
	}
	if (message.length < 6 || message[4] !== 0x1B) return
	if (++resends <= 3 || resends % 20 === 0) out('[CubaseSixty Helper] lists requested (' + resends + ')')
	for (var i = 0; i < CHANNELS; ++i) sendAll(activeDevice, i)
	for (var s = 0; s < REC_CHANNELS; ++s) sendRec(activeDevice, s)
	soloSent = -1
	sendSolo(activeDevice)
}

// Closing a project (Cubase shows the Hub) deactivates the scripts' drivers, opening one activates them again: logged.
deviceDriver.mOnActivate = function (activeDevice) { out('[CubaseSixty Helper] active (a project is open)') }
deviceDriver.mOnDeactivate = function (activeDevice) { out('[CubaseSixty Helper] inactive (no project)'); flushLog(activeDevice) }
deviceDriver.mOnIdle = function (activeDevice) { flushLog(activeDevice) }
