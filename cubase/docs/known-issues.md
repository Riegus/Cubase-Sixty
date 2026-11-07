# Known issues

Bugs seen in testing and noted for later. Each entry: what happens, where, and what is known about the cause.

## UF1

- **◀ ▶ (and the CHANNEL encoder) do not skip hidden tracks.** Stepping to the previous / next track lands on
  tracks that are hidden. The bridge sends notes 40 / 41, which the UF1 script binds to Cubase's
  `Project / Select Track: Prev / Next` commands; those step through hidden tracks too. Probably also the UC1's
  CHANNEL encoder, which uses the same commands (notes 100 / 101).
