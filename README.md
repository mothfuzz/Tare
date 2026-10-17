# Tare

A plugin (VST3 and CLAP) that stands in for a hardware synth. It
measures the MIDI out → synth → audio in round trip and reports it to the host
as plugin latency, so the host's delay compensation lines the synth up with
everything else.

<img width="488" height="483" alt="image" src="https://github.com/user-attachments/assets/a3518c76-4ea3-4b42-9512-e7be1add8958" />

It is built in two variants from the same code:

| Plugin | Type | Synth return arrives on | Use in |
|---|---|---|---|
| Tare | Instrument | Sidechain input | Ableton, and hosts that route audio into instruments |
| Tare FX | Audio effect | Main input | FL Studio, which can't route audio into instruments but does route MIDI to and from effects |

- Incoming MIDI is passed to the plugin's MIDI output. Route that to the synth.
- The synth's audio comes back on the **Synth Return** input and is passed to
  the plugin's output.
- **Calibrate** mutes the output, measures the noise floor, sends N pings, finds
  each onset in the return, and reports the median round trip as latency.
- **Latency trim** adds a manual offset (in ms) to the reported latency.

## Build

Requires CMake 3.22+ and git. JUCE 9.0.4 and clap-juce-extensions are fetched
automatically on the first configure.

```sh
cmake -B build                                   # or add -G Ninja, etc.
cmake --build build --config Release
ctest --test-dir build -C Release                # offline probe test
```

Outputs:

- `build/Tare_artefacts/Release/VST3/Tare.vst3`
- `build/Tare_artefacts/Release/CLAP/Tare.clap`
- `build/TareFX_artefacts/Release/VST3/Tare FX.vst3`
- `build/TareFX_artefacts/Release/CLAP/Tare FX.clap`

Copy them into `C:\Program Files\Common Files\VST3` and
`C:\Program Files\Common Files\CLAP` respectively.

Options: `-DTARE_BUILD_CLAP=OFF`, `-DTARE_BUILD_TESTS=OFF`.

## Calibrating

1. Wire it up: plugin MIDI out → synth, synth audio → the plugin's sidechain.
2. Pick a patch with a sharp attack, or lower the threshold.
3. Stop the transport and keep the synth quiet, then press **Calibrate**.
4. Press **Calibrate** again. Reporting a new latency can change how the host
   compensates the return path, so repeat until the result says *stable*.

The measured value is stored in the project. If the sample rate changes, it is
scaled as a best guess and you are prompted to recalibrate. Recalibrate after
changing the audio interface's buffer size too: the plugin can't detect that.

## FL Studio (Tare FX)

1. Put **Tare FX** on a mixer track whose input is the synth's audio.
2. In the plugin wrapper's MIDI settings, give it an input port and an output
   port. Send your MIDI to the input port (for example from a MIDI Out channel
   on the same port), and map the output port to the synth's hardware MIDI
   device in FL's MIDI settings. Use different port numbers for input and
   output, or the plugin's output will loop back into it.
3. Make sure the plugin isn't put to sleep while idle (smart disable), or
   calibration won't run.
