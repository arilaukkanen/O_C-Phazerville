// Copyright (c) 2021, Benjamin Rosenbach
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#define HEM_SHREDDER_ANIMATION_SPEED 500
#define HEM_SHREDDER_DOUBLE_CLICK_DELAY 5000

class Shredder : public HemisphereApplet {
public:
    enum ShredCursor {
      CHAN1_RANGE, CHAN2_RANGE,
      QUANT_CHAN,
      QUANT_EDIT,
      WRITE1, WRITE2, UNDO,
      RESETSHRED1, RESETSHRED2,

      MAX_CURSOR = RESETSHRED2
    };

    const char* applet_name() {
        return "Shredder";
    }
    const uint8_t* applet_icon() { return PhzIcons::shredder; }

    void Start() {
        StopWrite();
        undo_valid = false;
        step = 0;
        replay = 0;
        reset = true;
        quant_channels = 0;
        ForEachChannel(ch) {
            Shred(ch, true);
        }
        VoltageOut();
    }

    void Reset() {
        StopWrite();
        step = 0; // Reset
        reset = true;
        VoltageOut();
        ForEachChannel(ch) {
          if (shred_on_reset[ch])
            Shred(ch, true);
        }
    }

    void Controller() {
        if (recording_channel >= 0 && !EditMode()) StopWrite();
        if (Clock(1)) {
            Reset();
        }
        const bool gate_held = WriteGateHeld();
        if (gate_held != write_gate_was_held) {
            write_gate_was_held = gate_held;
            VoltageOut();
        }

        if (Clock(0)) {
            // Are the X or Y position being set? If so, get step coordinates. Otherwise,
            // simply play current step and advance it. This way, the applet can be used as
            // a more conventional arpeggiator as well as a Cartesian one.
            if (DetentedIn(0) || DetentedIn(1)) {
                int x = ProportionCV(In(0), 4, HEMISPHERE_MAX_INPUT_CV);
                int y = ProportionCV(In(1), 4, HEMISPHERE_MAX_INPUT_CV);
                if (x > 3) x = 3;
                if (y > 3) y = 3;
                step = (y * 4) + x;
                CommitWrite();
                VoltageOut();
            } else {
                if (!reset) {
                    ++step;
                }
                reset = false;
                if (step > 15) step = 0;
                CommitWrite();
                VoltageOut();
            }
            replay = 0;
        } else if (replay) {
            VoltageOut();
            replay = 0;
        }

        // Handle imprint confirmation animation
        if (--confirm_animation_countdown < 0) {
            confirm_animation_position--;
            confirm_animation_countdown = HEM_SHREDDER_ANIMATION_SPEED;
        }
    }

    void View() {
        DrawParams();
        DrawGrid();
    }

    void AuxButton() {
      if (recording_channel >= 0) return;
      if (cursor < 2) {
        Shred(cursor, true);
      }
      else if (cursor == UNDO)
        UndoWrite();
      else
        CancelEdit();
    }

    void OnButtonPress() {
      if (recording_channel >= 0) {
        StopWrite();
        return;
      }
      switch (cursor) {
        case CHAN1_RANGE:
        case CHAN2_RANGE:
          if (OC::CORE::ticks - click_tick < HS::HEMISPHERE_DOUBLE_CLICK_TIME)
            Shred(cursor, true);
          else
            click_tick = OC::CORE::ticks;
        default:
          CursorToggle();
          break;

        case QUANT_EDIT:
          HS::QuantizerEdit(io_offset);
          break;

        case RESETSHRED1:
        case RESETSHRED2:
          shred_on_reset[cursor - RESETSHRED1] = !shred_on_reset[cursor - RESETSHRED1];
          break;

        case WRITE1:
        case WRITE2:
          StartWrite(cursor - WRITE1);
          break;

        case UNDO:
          UndoWrite();
          break;
      }
    }

    void OnEncoderMove(int direction) {
        if (recording_channel >= 0) {
            if (!WriteGateHeld()) return;
            const int ch = recording_channel;
            const int limit = range[ch] * (12 << 7);
            const int minimum = bipolar[ch] ? -limit : 0;
            const int increment = (limit - minimum) / 24;
            const int variation = increment / 4;
            const int nominal = constrain((int)nominal_voltage + direction * increment, minimum, limit);
            int voltage = nominal;
            // Keep endpoints exact; vary interior detents by at most 25% of a step.
            if (direction && nominal > minimum && nominal < limit)
                voltage += random(-variation, variation + 1);
            noInterrupts();
            nominal_voltage = nominal;
            held_voltage = voltage;
            interrupts();
            VoltageOut();
            return;
        }
        if (!EditMode()) {
            MoveCursor(cursor, direction, MAX_CURSOR);
            return;
        }

        if (cursor < 2) {
            range[cursor] += direction;
            if (bipolar[cursor]) {
                if (range[cursor] > 3) {
                    range[cursor] = 0;
                    bipolar[cursor] = false;
                } else if (range[cursor] < 1) {
                    range[cursor] = 5;
                    bipolar[cursor] = false;
                }
            } else {
                if (range[cursor] > 5) {
                    range[cursor] = 1;
                    bipolar[cursor] = true;
                } else if (range[cursor] < 0) {
                    range[cursor] = 3;
                    bipolar[cursor] = true;
                }
            }
        }
        if (QUANT_CHAN == cursor)
          quant_channels = constrain(quant_channels + direction, 0, 2);
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        // Not enough room to save the sequences, so we'll just have to save settings
        Pack(data, PackLocation {0,4}, range[0]); // range will never be more than 4 bits
        Pack(data, PackLocation {4,1}, int(bipolar[0]));
        Pack(data, PackLocation {5,1}, shred_on_reset[0]);
        Pack(data, PackLocation {8,4}, range[1]);
        Pack(data, PackLocation {12,1}, int(bipolar[1]));
        Pack(data, PackLocation {13,1}, shred_on_reset[1]);
        Pack(data, PackLocation {16,8}, quant_channels);
        Pack(data, PackLocation {24,8}, GetScale(0));
        Pack(data, PackLocation {32, 16}, seed[0]);
        Pack(data, PackLocation {48, 16}, seed[1]);
        return data;
    }

    void OnDataReceive(uint64_t data) {
        StopWrite();
        undo_valid = false;
        range[0] = Unpack(data, PackLocation {0,4}); // only 4 bits used for range
        bipolar[0] = Unpack(data, PackLocation {4,1}); 
        shred_on_reset[0] = Unpack(data, PackLocation {5,1});
        range[1] = Unpack(data, PackLocation {8,4});
        bipolar[1] = Unpack(data, PackLocation {12,1}); 
        shred_on_reset[1] = Unpack(data, PackLocation {13,1});
        quant_channels = Unpack(data, PackLocation {16,8});
        SetScale(0, Unpack(data, PackLocation {24,8}));
        seed[0] = Unpack(data, PackLocation {32, 16});
        seed[1] = Unpack(data, PackLocation {48, 16});
        ForEachChannel(ch) {
            Shred(ch, false);
        }
        VoltageOut();
    }

    void Unload() { StopWrite(); }
    void OnUIExit() { StopWrite(); }
    bool IsWriteGateArmed() { return recording_channel >= 0; }

protected:
    void SetHelp() {
        //                    "-------" <-- Label size guide
        help[HELP_DIGITAL1] = "Clock";
        help[HELP_DIGITAL2] = "Reset";
        help[HELP_CV1]      = "X pos";
        help[HELP_CV2]      = "Y pos";
        help[HELP_OUT1]     = "Ch 1";
        help[HELP_OUT2]     = "Ch 2";
        help[HELP_EXTRA1] = "";
        help[HELP_EXTRA2] = "Hold Aux: WRITE";
        //                  "---------------------" <-- Extra text size guide
    }

private:
    int cursor;
    uint32_t click_tick = 0;
    uint16_t seed[2] = {0,0};
    int8_t recording_channel = -1;
    bool write_gate_was_held = false;
    int16_t nominal_voltage = 0;
    int16_t held_voltage = 0;
    int16_t undo_steps[16];
    int8_t undo_channel = -1;
    bool undo_valid = false;

    // Sequencer state
    uint8_t step; // Current step number
    int sequence[2][16];
    int current[2];
    bool replay; // When the encoder is moved, re-quantize the output
    bool reset;

    // settings
    int range[2] = {1,0};
    bool bipolar[2] = {false, false};
    bool shred_on_reset[2] = {false, false};
    int8_t quant_channels;

    // Variables to handle imprint confirmation animation
    int confirm_animation_countdown;
    int confirm_animation_position;

    void DrawParams() {
        // Channel 1 voltage
        char outlabel[] = { (char)('A' + io_offset), ':', '+', '\0' };
        gfxPrint(1, 15, outlabel);
        if (shred_on_reset[0]) gfxInvert(1, 15, 12, 8);
        gfxPrint(19, 15, (char) (range[0]));
        if (bipolar[0]) {
          gfxPrint(13, 18, "-");
        }
        if (CHAN1_RANGE == cursor) gfxSpicyCursor(13, 23, 12);

        // Channel 2 voltage
        ++outlabel[0];
        gfxPrint(33, 15, outlabel);
        if (shred_on_reset[1]) gfxInvert(33, 15, 12, 8);
        gfxPrint(51, 15, (char) (range[1]));
        if (bipolar[1]) {
          gfxPrint(45, 18, "-");
        }
        if (CHAN2_RANGE == cursor) gfxSpicyCursor(45, 23, 12);

        if (cursor >= RESETSHRED1 && cursor <= RESETSHRED2) {
          gfxIcon(1 + 32*(cursor-RESETSHRED1), 8, DOWN_BTN_ICON);
        }

        if (recording_channel >= 0) {
          gfxPrint(33, 25, WriteGateHeld() ? "REC " : "ARM ");
          gfxPrint(OutputLabel(recording_channel));
          const int tenths = (abs(held_voltage) * (NorthernLightModular ? 12 : 10) + 768) / 1536;
          gfxPos(33, 35);
          gfxPrint(held_voltage < 0 ? "-" : "+");
          gfxPrint(tenths / 10);
          gfxPrint(".");
          gfxPrint(tenths % 10);
          gfxPrint("V");
        } else {
          // quantize channel selection
          gfxIcon(32, 25, SCALE_ICON);
          if (quant_channels == 0) {
            outlabel[0] = 'A'+io_offset;
            outlabel[1] = '+';
            outlabel[2] = 'B'+io_offset;
            gfxPrint(42, 25, outlabel);
          } else {
            gfxPrint(48, 25, OutputLabel(quant_channels - 1));
          }
          if (QUANT_CHAN == cursor) gfxCursor(42, 33, 20);

          // quantize scale selection
          gfxPrint(32, 35, OC::scale_names_short[GetScale(0)]);
          if (QUANT_EDIT == cursor) gfxCursor(32, 43, 30);
        }

        gfxPrint(33, 47, "W");
        gfxPrint(OutputLabel(0));
        gfxPrint(49, 47, "W");
        gfxPrint(OutputLabel(1));
        if (WRITE1 == cursor) gfxCursor(33, 54, 12);
        if (WRITE2 == cursor) gfxCursor(49, 54, 12);
        gfxPrint(34, 56, "UNDO");
        if (UNDO == cursor) gfxCursor(34, 63, 24);

    }

    void DrawGrid() {
        // Draw the Cartesian plane
        for (int s = 0; s < 16; s++) gfxFrame(1 + (8 * (s % 4)), 26 + (8 * (s / 4)), 5, 5);

        // Crosshairs for play position
        int cxy = step / 4;
        int cxx = step % 4;
        gfxDottedLine(3 + (8 * cxx), 26, 3 + (8 * cxx), 58, 2);
        gfxDottedLine(1, 28 + (8 * cxy), 32, 28 + (8 * cxy), 2);
        gfxRect(1 + (8 * cxx), 26 + (8 * cxy), 5, 5);

        // Draw imprint animation, if necessary
        if (confirm_animation_position > -1) {
            int progress = 16 - confirm_animation_position;
            for (int s = 0; s < progress; s++)
            {
                gfxRect(1 + (8 * (s / 4)), 26 + (8 * (s % 4)), 7, 7);
            }
        }
    }

    void Shred(int ch, bool reseed) {
        StopWrite();
        undo_valid = false;
        int max;
        int min;
        if (reseed) {
            // generate a random seed value and then reseed using that value
            randomSeed(micros());
            seed[ch] = random(0, 65535); // 16 bits
        }
        randomSeed(seed[ch]);
        for (int i = 0; i < 16; i++) {
            if (range[ch] == 0) {
                sequence[ch][i] = 0;
            } else {
                max = range[ch] * (12 << 7);
                min = bipolar[ch] ? -max : 0;
                sequence[ch][i] = random(min, max);
            }
        }

        // start imprint animation
        confirm_animation_position = 16;
        confirm_animation_countdown = HEM_SHREDDER_ANIMATION_SPEED;
    }

    void VoltageOut() {
        ForEachChannel(ch) {
            current[ch] = ch == recording_channel && WriteGateHeld() ? held_voltage : sequence[ch][step];
            int8_t qc = quant_channels - 1; 
            if (qc < 0 || qc == ch) current[ch] = Quantize(0, current[ch]);
            Out(ch, current[ch]);
        }
    }

    void StartWrite(int ch) {
        noInterrupts();
        undo_channel = ch;
        for (int i = 0; i < 16; ++i) undo_steps[i] = sequence[ch][i];
        undo_valid = true;
        const int limit = range[ch] * (12 << 7);
        nominal_voltage = constrain(sequence[ch][step], bipolar[ch] ? -limit : 0, limit);
        held_voltage = nominal_voltage;
        if (!EditMode()) CursorToggle();
        recording_channel = ch;
        write_gate_was_held = WriteGateHeld();
        // Holding UP is the left hemisphere's write gate, not a screensaver gesture.
        if (io_offset == 0) OC::ui._preemptScreensaver(true);
        interrupts();
        VoltageOut();
    }

    void StopWrite() {
        if (recording_channel < 0) return;
        recording_channel = -1;
        write_gate_was_held = false;
        if (io_offset == 0) OC::ui._preemptScreensaver(false);
        CancelEdit();
        VoltageOut();
    }

    void CommitWrite() {
        if (WriteGateHeld()) sequence[recording_channel][step] = held_voltage;
    }

    bool WriteGateHeld() {
        return recording_channel >= 0 && OC::ui.read_immediate(
            io_offset == 0 ? OC::CONTROL_BUTTON_A : OC::CONTROL_BUTTON_B);
    }

    void UndoWrite() {
        noInterrupts();
        const bool restore = undo_valid && recording_channel < 0;
        if (restore) {
            for (int i = 0; i < 16; ++i) sequence[undo_channel][i] = undo_steps[i];
            undo_valid = false;
        }
        interrupts();
        if (restore) VoltageOut();
    }
};
