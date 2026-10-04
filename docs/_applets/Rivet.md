---
title: Rivet
category: Other
---

Rivet is a three-oscillator, BIA-inspired percussion voice for standard Teensy 3.2 O_C hardware. It combines decaying sines, phase modulation, a short pitch drop, noise, and folding. It is a new instrument, not a Basimilus Iteritas Alter clone. The output uses the O_C CV DAC at the firmware's 16.67 kHz core rate, so expect a gritty, bandwidth-limited sound and test it through an attenuated mixer input.

### Logical I/O

| Input/output | Function |
| --- | --- |
| Trigger 1 | Normal hit |
| Trigger 2 | Accented hit (takes precedence if both arrive together) |
| CV 1 | Assignable modulation; default is continuous pitch at approximately 1 V/oct |
| CV 2 | Assignable modulation; default is Timbre (Metal, Fold and Noise together) |
| Output 1 | Bipolar audio, limited to about ±2 V |
| Output 2 | Unipolar envelope, 0–5 V |

The logical inputs follow Hemisphere's current Input Mapping. On the left half, outputs are A/B; on the right, C/D. Check the active Input Mapping before patching triggers or CV.

### Controls

Turn the encoder to select a parameter. Press to edit; turn to change its value; press again to leave editing. **LPF** controls the low-pass filter after the wavefolder: low values make darker, rounder hits; high values let more metallic brightness through. **CV1** and **CV2** independently select what the two mapped CV inputs control. After CV2, select **RANDOM** and press to randomize the seven voice parameters. LPF and both CV assignments stay at their chosen values. The list scrolls to show the selected item.

| Parameter | Effect |
| --- | --- |
| Pitch | Base frequency, roughly 35–350 Hz before CV |
| Decay | Main body, roughly 35–980 ms |
| Spread | Frequency spacing of the second and third oscillator |
| Metal | Phase-modulation depth |
| Fold | Symmetric wavefold drive |
| Noise | Level and length of the noisy attack |
| Sweep | Fast pitch-envelope amount |
| LPF | Post-fold low-pass cutoff, dark to bright; 20 matches the earlier fixed filter |

Each CV input can be set independently. Turning the encoder cycles through **Off**, **1V/oct**, **Timbre**, then the six three-control macros below, followed by the individual controls and paired options. **1V/oct** tracks pitch continuously (subject to the engine's pitch limits). **Timbre** moves Metal, Fold and Noise together; Noise receives half the modulation depth and its burst length stays fixed. The newer macros offer broader three-parameter movements:

| On screen | CV controls | Suggested use |
| --- | --- | --- |
| Attack | Noise + Sweep + Metal | Sharper, noisier zaps |
| Impact | Decay + Sweep + Fold | Heavier hits and kicks |
| Rattle | Spread + Metal + Noise | Clanging, irregular attacks |
| Crunch | Fold + Metal + LPF | Bright distorted hits |
| Air | Noise + Fold + LPF | Airy-to-bristling attacks |
| Body | Decay + Spread + Fold | Longer, denser body |

The CV value adds to each destination's panel setting and clips at its valid range. Positive CV raises the named controls; negative CV lowers them. Both inputs may address the same destination and their modulation adds. Decay and Noise burst length update on the next hit; other timbres respond during a hit. LPF's knob remains its base cutoff even when CV modulates it.

The paired options give one CV several destinations at once, using the same modulation depths as the individual options:

| On screen | CV controls |
| --- | --- |
| M+F | Metal + Fold |
| F+N | Fold + Noise |
| N+D | Noise + Decay |
| D+Sw | Decay + Sweep |
| Sp+M | Spread + Metal |
| F+LPF | Fold + LPF |

The individual **Metal**, **Fold**, **Noise**, **Decay**, **Spread**, **Sweep**, and **LPF** targets remain selectable for now, as do the paired options above. This preserves access to existing sounds while the macro set is evaluated; their preset IDs remain stable so a later, shorter CV menu can still restore old presets. Useful assignments include **1V/oct + Timbre** (default), **1V/oct + Impact** for pitched kicks, **1V/oct + Attack** for zaps, and **LPF + Crunch** for two-dimensional sweeps. Older Rivet presets retain their CV assignments; presets from before CV assignment was added load with the default 1V/oct + Timbre pair.

Settings are stored with the Hemisphere preset. The active voice is monophonic: a new trigger fades the previous audio over about 1 ms, then restarts the hit. An accent raises the new hit's output level by 25%; simultaneous triggers produce one accented hit. CV changes are read about once per millisecond. The final envelope starts with a short ramp to reduce clicks.

### Starting patches

| Sound | Pitch | Decay | Spread | Metal | Fold | Noise | Sweep | LPF |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Distorted kick | 8 | 34 | 3 | 4 | 12 | 3 | 55 | 20 |
| Metallic hit | 28 | 27 | 52 | 58 | 51 | 10 | 22 | 40 |
| Noisy attack | 25 | 16 | 38 | 35 | 35 | 60 | 22 | 28 |

The offline WAV previews in `software/previews/rivet/` use the same DSP source, but do not model the O_C DAC, output calibration, analog filtering or downstream gain. Listen on the module before judging the final sound.

### Hardware check

Build the `T32_nightwire` environment, select Rivet in a Hemisphere slot, and route output 1 through an attenuator or mixer. Test the starting patches at low monitoring level. Patch triggers to the logical inputs shown in the current Input Mapping, and CV to the mapped CV inputs. Try normal and accented simultaneous triggers, quick retriggers, and a slow CV2 sweep. Then pair Rivet with a busy applet on the other side. The firmware's DEBUG core page reports ISR minimum/average/maximum in microseconds; watch the maximum while both sides run. The 60 µs period is the deadline. A successful build and desktop tests cannot establish that the worst-case hardware ISR meets it.
