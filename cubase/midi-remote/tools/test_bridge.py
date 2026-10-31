"""Plays the bridge for the Cubase-Sixty MIDI Remote script.

Sends test messages to "Cubase-Sixty UF8 to Cubase" and prints whatever Cubase
sends back on "Cubase-Sixty UF8 from Cubase". Requires: pip install mido python-rtmidi
Run it with the MixConsole open. See ../README.md.
"""
import time

import mido


def find(names, key):
    return next(n for n in names if n.startswith("Cubase-Sixty UF8 " + key))


def describe(msg):
    if msg.type == "sysex" and len(msg.data) > 4 and tuple(msg.data[:3]) == (0x7D, 0x52, 0x36):
        cmd, strip, payload = msg.data[3], msg.data[4], msg.data[5:]
        if cmd == 1:
            return f"NAME strip {strip + 1}: {bytes(payload).decode('ascii', 'replace')!r}"
        if cmd == 2 and len(payload) == 3:
            return f"COLOUR strip {strip + 1}: r={payload[0]} g={payload[1]} b={payload[2]}"
    return str(msg)


out = mido.open_output(find(mido.get_output_names(), "to Cubase"))
inp = mido.open_input(find(mido.get_input_names(), "from Cubase"),
                      callback=lambda m: print("FROM CUBASE:", describe(m)))


def send(label, *msgs):
    print("SEND:", label)
    for m in msgs:
        out.send(m)
    time.sleep(2)


def note(n, on=True):
    return mido.Message("note_on", channel=0, note=n, velocity=127 if on else 0)


def pitch(fraction):
    return mido.Message("pitchwheel", channel=0, pitch=int(fraction * 16383) - 8192)


send("fader 1 to 75%", pitch(0.75))
send("fader 1 to 25%", pitch(0.25))
send("mute track 1 on", note(16))
send("mute track 1 off", note(16))      # toggle: send again to switch it off
send("solo track 2 on", note(9))
send("solo track 2 off", note(9))       # toggle: send again to switch it off
send("bank right", note(47))
send("bank left", note(46))
send("play", note(94))
send("stop", note(93))

print("Done. Rename a track or change its colour in Cubase and watch for NAME / COLOUR lines.")
print("Ctrl-C to quit.")
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    pass
