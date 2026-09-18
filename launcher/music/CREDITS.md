# Launcher Music

- Composition: **Cantina Band**, by John Williams.
- MIDI source: **John Williams - Cantina Band MIDI**, Nonstop2k.
- Source URL: https://www.nonstop2k.com/midi-files/11610-john-williams-cantina-band-midi.html
- Source file: `john-williams-cantina-band-11610-nonstop2k.com.mid`.
- Chiptune conversion: the OpenJedvibe project, with
  `scripts/compile-launcher-music.py` and `launcher/music_synth.cpp`.

The MIDI is not included in the repository or the package. Builds use the
checked-in `cantina-band.score` file (114,988 bytes). Python and the MIDI file
are not needed to build or run the launcher. The launcher generates stereo
audio in real time with pulse, triangle, and noise instruments. It does not use
a recorded soundtrack, a sound bank, or a WAV file.

## Convert the Music Again

1. Get the MIDI from the source URL. Follow the source terms.
2. Keep the downloaded MIDI outside the repository.
3. From the repository root, run:

   ```sh
   python3 scripts/compile-launcher-music.py /path/to/john-williams-cantina-band-11610-nonstop2k.com.mid launcher/music/cantina-band.score
   ```

4. Build the launcher and review the generated audio. Commit the score, not the MIDI.

The converter needs Python 3. It uses only the Python standard library.

The source page describes educational and remix use. The project owner supplied
this file for that use. This credit does not grant rights to the composition or
change the source terms. Check those rights before distribution. Do not apply
the project's source-code licence to the MIDI, the derived score, or the generated audio.

## Band Sprite

The OpenJedvibe horn-player sprite is original pixel art. Its source is
`scripts/build-launcher-band.py`. It uses hand-authored pixel patterns, not
pixels from a photograph. The local cantina reference image is not included
in the repository or the package.
