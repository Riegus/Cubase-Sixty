// Cubase-Sixty – MIDI Remote script for the SSL UF1 (via the Cubase-Sixty Bridge)
//
// The UF1 is one channel strip, so it controls the SELECTED track:
// fader (with touch), SOLO / CUT, transport keys, SEL and the V-pot pushes.
// The MIDI it speaks is the UF8's strip-1 numbering (see cubase/docs/bridge-midi-protocol.md).
//
// Install in
//   Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\UF1\
// (tools\install-scripts.ps1 does this for you.)

var midiremote_api = require('midiremote_api_v1')

console.log('[CubaseSixty UF1] script loaded, version 0.1.0-dev')

var deviceDriver = midiremote_api.makeDeviceDriver('CubaseSixty', 'UF1', 'Riegus')

var midiInput = deviceDriver.mPorts.makeMidiInput()
var midiOutput = deviceDriver.mPorts.makeMidiOutput()

deviceDriver
	.makeDetectionUnit()
	.detectPortPair(midiInput, midiOutput)
	.expectInputNameEquals('Cubase-Sixty UF1 to Cubase')
	.expectOutputNameEquals('Cubase-Sixty UF1 from Cubase')

var surface = deviceDriver.mSurface

// ---------------------------------------------------------------------------
// Surface layout
// ---------------------------------------------------------------------------

var fader = surface.makeFader(0, 4, 2, 8)
var faderTouch = surface.makeCustomValueVariable('faderTouch')
var solo = surface.makeButton(3, 1, 1, 1)
var cut = surface.makeButton(4, 1, 1, 1)
var sel = surface.makeButton(5, 1, 1, 1)
var vpot = surface.makeKnob(0, 2, 2, 2)

fader.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToPitchBend(0)
faderTouch.mMidiBinding
	.setInputPort(midiInput)
	.bindToNote(0, 104)
solo.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 8)
cut.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 16)
sel.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 24)
vpot.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToControlChange(0, 16).setTypeRelativeSignedBit()

// Transport keys (notes 86/87/91..95), with LED feedback.
function transportButton(x, note) {
	var button = surface.makeButton(x, 13, 1, 1)
	button.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, note)
	return button
}
var rwd = transportButton(0, 91)
var ffw = transportButton(1, 92)
var stop = transportButton(2, 93)
var play = transportButton(3, 94)
var rec = transportButton(4, 95)
var cycle = transportButton(5, 86)
var click = transportButton(6, 87)

// ---------------------------------------------------------------------------
// Mapping: everything acts on the selected track
// ---------------------------------------------------------------------------

var page = deviceDriver.mMapping.makePage('Selected track')

var track = page.mHostAccess.mTrackSelection.mMixerChannel.mValue
var transport = page.mHostAccess.mTransport.mValue

page.makeValueBinding(fader.mSurfaceValue, track.mVolume).setValueTakeOverModeJump()
page.makeValueBinding(solo.mSurfaceValue, track.mSolo).setTypeToggle()
page.makeValueBinding(cut.mSurfaceValue, track.mMute).setTypeToggle()
page.makeValueBinding(sel.mSurfaceValue, track.mEditorOpen).setTypeToggle()
page.makeValueBinding(vpot.mSurfaceValue, track.mPan)

// Fader touch: lets Cubase treat the fader as touch-sensitive for automation (API 1.1+).
if (fader.mSurfaceValue.mTouchState) {
	fader.mSurfaceValue.mTouchState.bindTo(faderTouch)
}

page.makeValueBinding(rwd.mSurfaceValue, transport.mRewind)
page.makeValueBinding(ffw.mSurfaceValue, transport.mForward)
page.makeValueBinding(stop.mSurfaceValue, transport.mStop).setTypeToggle()
page.makeValueBinding(play.mSurfaceValue, transport.mStart).setTypeToggle()
page.makeValueBinding(rec.mSurfaceValue, transport.mRecord).setTypeToggle()
page.makeValueBinding(cycle.mSurfaceValue, transport.mCycleActive).setTypeToggle()
page.makeValueBinding(click.mSurfaceValue, transport.mMetronomeActive).setTypeToggle()

