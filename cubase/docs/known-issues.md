# Known issues

Bugs seen in testing and noted for later. Each entry: what happens, where, and what is known about the cause.

## UF1

- **◀ ▶ (and the CHANNEL encoder) do not skip hidden tracks.** Stepping to the previous / next track lands on
  tracks that are hidden. The bridge sends notes 40 / 41, which the UF1 script binds to Cubase's
  `Project / Select Track: Prev / Next` commands; those step through hidden tracks too. Probably also the UC1's
  CHANNEL encoder, which uses the same commands (notes 100 / 101).
- **Track change may lag a little** (since the CHANNEL encoder push / SHIFT + turn, 7.11.2026). Stepping the track
  with the UF1's CHANNEL encoder felt slower; sixteen insert viewers were replaced by one, which helped, but it is not
  sure it is as quick as before. To check against `cubase` without the change; suspects: the PLUGIN scan's extra slot
  reads, the insert viewer.
