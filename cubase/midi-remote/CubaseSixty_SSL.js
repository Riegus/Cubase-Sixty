// Cubase-Sixty – MIDI Remote script for SSL UF8 (via the Cubase-Sixty Bridge)
//
// Draft v0: eight faders with touch, SEL/SOLO/CUT, banking and transport.
// The MIDI it speaks is defined in cubase/docs/bridge-midi-protocol.md.
//
// Cubase looks for this file in
//   Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\SSL\
// See the repo README for linking that folder to this one.

var midiremote_api = require('midiremote_api_v1')

var deviceDriver = midiremote_api.makeDeviceDriver('CubaseSixty', 'SSL', 'Riegus')

var midiInput = deviceDriver.mPorts.makeMidiInput()
var midiOutput = deviceDriver.mPorts.makeMidiOutput()

deviceDriver
	.makeDetectionUnit()
	.detectPortPair(midiInput, midiOutput)
	.expectInputNameEquals('Cubase-Sixty UF8')
	.expectOutputNameEquals('Cubase-Sixty UF8')

var surface = deviceDriver.mSurface

// ---------------------------------------------------------------------------
// Surface layout (a simple on-screen picture of the UF8)
// ---------------------------------------------------------------------------

var STRIPS = 8
var strips = []

for (var i = 0; i < STRIPS; ++i) {
	var x = i * 3
	var strip = {}

	strip.select = surface.makeButton(x, 0, 2, 1)
	strip.solo = surface.makeButton(x, 1, 1, 1)
	strip.cut = surface.makeButton(x + 1, 1, 1, 1)
	strip.vpot = surface.makeKnob(x, 2, 2, 2)
	strip.fader = surface.makeFader(x, 4, 2, 8)
	strip.faderTouch = surface.makeCustomValueVariable('faderTouch' + (i + 1))

	strip.select.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToNote(0, 24 + i)
	strip.solo.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToNote(0, 8 + i)
	strip.cut.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToNote(0, 16 + i)
	strip.vpot.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput)
		.bindToControlChange(0, 16 + i)
		.setTypeRelativeSignedBit()
	strip.fader.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToPitchBend(i)
	strip.faderTouch.mMidiBinding
		.setInputPort(midiInput)
		.bindToNote(0, 104 + i)

	strips.push(strip)
}

var bankLeft = surface.makeButton(0, 13, 2, 1)
var bankRight = surface.makeButton(2, 13, 2, 1)
var stopButton = surface.makeButton(6, 13, 2, 1)
var playButton = surface.makeButton(8, 13, 2, 1)
var recordButton = surface.makeButton(10, 13, 2, 1)

bankLeft.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 46)
bankRight.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 47)
stopButton.mSurfaceValue.mMidiBinding
	.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 93)
playButton.mSurfaceValue.mMidiBinding
	.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 94)
recordButton.mSurfaceValue.mMidiBinding
	.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 95)

// ---------------------------------------------------------------------------
// Mapping: mixer page
// ---------------------------------------------------------------------------

var page = deviceDriver.mMapping.makePage('Mixer')

var bankZone = page.mHostAccess.mMixConsole
	.makeMixerBankZone()
	.excludeInputChannels()
	.excludeOutputChannels()

for (var i = 0; i < STRIPS; ++i) {
	var channel = bankZone.makeMixerBankChannel()
	var strip = strips[i]

	page.makeValueBinding(strip.fader.mSurfaceValue, channel.mValue.mVolume)
		.setValueTakeOverModeJump()
	page.makeValueBinding(strip.vpot.mSurfaceValue, channel.mValue.mPan)
	page.makeValueBinding(strip.select.mSurfaceValue, channel.mValue.mSelected)
	page.makeValueBinding(strip.solo.mSurfaceValue, channel.mValue.mSolo)
		.setTypeToggle()
	page.makeValueBinding(strip.cut.mSurfaceValue, channel.mValue.mMute)
		.setTypeToggle()

	// Fader touch: tell Cubase the fader is held, so automation in Touch mode
	// behaves like a touch-sensitive fader.
	page.makeValueBinding(strip.faderTouch, channel.mValue.mVolume.mTouched)
}

page.makeActionBinding(bankLeft.mSurfaceValue, bankZone.mAction.mPrevBank)
page.makeActionBinding(bankRight.mSurfaceValue, bankZone.mAction.mNextBank)

var transport = page.mHostAccess.mTransport.mValue
page.makeValueBinding(stopButton.mSurfaceValue, transport.mStop).setTypeToggle()
page.makeValueBinding(playButton.mSurfaceValue, transport.mStart).setTypeToggle()
page.makeValueBinding(recordButton.mSurfaceValue, transport.mRecord).setTypeToggle()
