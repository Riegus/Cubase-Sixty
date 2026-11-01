"""Checks the virtual ports the bridge creates, without Cubase.

Run the bridge with --trace first, then this script. It
  1. prints the MIDI ports Windows sees,
  2. listens on "Cubase-Sixty UF8 to Cubase" for 5 seconds (move a UF8 fader or press a button),
  3. sends a note to "Cubase-Sixty UF8 from Cubase" (the bridge should print "[virtual port in]").
Requires: pip install mido python-rtmidi
"""
import time

import mido

print("Inputs: ", mido.get_input_names())
print("Outputs:", mido.get_output_names())

inn = next((n for n in mido.get_input_names() if "Cubase-Sixty UF8 to Cubase" in n), None)
out = next((n for n in mido.get_output_names() if "Cubase-Sixty UF8 from Cubase" in n), None)
print("Using input:", inn, " output:", out)

if inn:
    print("Listening 5 s on the input; touch a UF8 fader or press a UF8 button...")
    with mido.open_input(inn) as port:
        end = time.time() + 5
        while time.time() < end:
            for msg in port.iter_pending():
                print("  received:", msg)
            time.sleep(0.01)
if out:
    print("Sending a note to the output...")
    with mido.open_output(out) as port:
        port.send(mido.Message("note_on", note=60, velocity=100))
        time.sleep(0.2)
        port.send(mido.Message("note_off", note=60))
print("Done. Did the bridge print '[virtual port in]' lines?")
