---
layout: default
---
# Shredder

![Shredder Screenshot](images/Shredder.png)

**Shredder** is a 2-channel, 16-step CV sequencer on a 4x4 grid, inspired by the [Noise Engineering Mimetic Digitalis](https://noiseengineering.us/products/mimetic-digitalis). Each channel can have random values "shred" to the sequence within a specified range. Ranges can be from 1 to 5 volts unipolar, or 1 to 3 volts bi-polar. Additionally, one or both channels can run through the quantizer.

[See it in action!](https://youtu.be/yLr3vQJm3wM)

### I/O

|        | 1/3 | 2/4 |
| ------ | :-: | :-: |
| TRIG   |  Clock   |  Reset   |
| CV INs | X position    |  Y position   |
| OUTs   |  CV channel 1   | CV channel 2    |

## UI Parameters

Clockwise cursor order: A/B voltage ranges, quantized channels, scale selection, W A/B (or W C/D), UNDO, then the two Shred-on-Reset toggles.

 * Octave range
 * Quantized channels
 * Quantizer edit
 * AuxButton: Shred selected channel
    - Highlight channel octave range and press the select button — UP or DOWN, depending on Hemisphere
 * **W A/B** on the left hemisphere, or **W C/D** on the right: move the cursor to a channel and press its encoder to arm WRITE. Hold that hemisphere's UP/DOWN button and turn its encoder to set the held voltage. Keep holding the button to write on clock pulses; release it to pause writing. Press the encoder again to disarm. The screen shows `ARM` while waiting and `REC` while the button is held, plus the output letter and held voltage.
 * **UNDO**: select and press the encoder after a take to restore that channel's preceding 16 steps. Only the latest take on each Shredder instance is available. Undo does not stop the clock.
 * (new in v1.8.3) Shred-on-Reset toggle, per channel
    - Highlighted means that channel will be randomized on Reset

WRITE draws a clocked, stepped voltage pattern. While the side button is held, the selected output previews encoder changes immediately and each incoming clock writes the held voltage to the step it visits, even when the encoder has stopped moving. With the button released, the channel resumes playback and the encoder does not change the held voltage. Arming WRITE alone never changes a step. Stopping incoming clocks pauses writing; press the encoder to disarm. The other channel continues to play. Each hemisphere can run its own Shredder instance, giving four CV outputs with two shared playheads.

Twenty-four encoder detents span the selected raw voltage range from minimum to maximum. The step size therefore follows the range (about 0.21 V per detent for 0–5 V, or 0.25 V for -3–+3 V) and clamps at both ends. Each turn within the range adds a random variation of at most 25% of one detent's voltage step (about ±0.052 V at 0–5 V, or ±0.063 V at -3–+3 V), so repeated visits to the same detent can produce different recorded values. The minimum and maximum remain exact. The chosen voltage stays fixed until the next encoder turn, including across clocks. In the +5 V range, the raw value can reach exactly 0 V and +5 V. The existing quantizer can alter the output voltage; disable it on the recording channel when exact raw endpoints matter. Range changes affect future shreds and WRITE limits; they do not rescale existing steps.

Reset stops WRITE before a coincident clock can write, then retains Shredder's step-1 playback behavior. **Shred-on-Reset destroys a recorded loop** on any channel where it is enabled; turn it off for a stable loop. Manual shred also replaces that channel's pattern and clears undo. X/Y CV inputs address the 4×4 grid during clocking. Leave them unpatched or unmapped for ordinary linear stepping.

**Recorded patterns and undo are temporary.** They live in RAM, and preset save still stores only settings and random seeds. Preset recall regenerates the seed pattern and clears WRITE and undo. Power-off loses hand-recorded steps. Switching away from Shredder stops WRITE; the host may retain the RAM pattern when returning, but retention is not guaranteed. Opening a menu, help screen, clock setup, or screensaver also stops WRITE.

As of v1.11, sequence seeds are now saved, which means they will regenerate the same sequence on preset load or power cycle!

### Credits
Copied/Adapted from [Shredder](https://github.com/benirose/O_C-BenisphereSuite/wiki/Shredder) by benirose
