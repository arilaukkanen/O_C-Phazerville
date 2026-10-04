#pragma once

#include "RivetDSP.h"

// A compact, BIA-inspired percussion voice for ordinary T32 DAC outputs.
class Rivet : public HemisphereApplet {
public:
    const char* applet_name() { return "Rivet"; }

    void Start() {
        cursor_ = 0;
        control_tick_ = 0;
        dsp_.SetModes(cv_mode_[0], cv_mode_[1]);
        dsp_.UpdateControls();
    }

    void Unload() override { dsp_.Silence(); }

    void Controller() {
        if (++control_tick_ >= 16) {
            control_tick_ = 0;
            dsp_.SetCV(In(0), In(1));
            dsp_.UpdateControls();
        }
        const bool accent = Clock(1);
        if (Clock(0) || accent) dsp_.Trigger(accent);
        int envelope = 0;
        const int audio = dsp_.Render(envelope);
        Out(0, audio);
        Out(1, envelope);
    }

    void View() {
        static const char* labels[8] = {
            "Pitch", "Decay", "Spread", "Metal", "Fold", "Noise", "Sweep", "LPF"
        };
        // Scroll only when the selected item is below the seventh row.
        const int first = cursor_ > 6 ? cursor_ - 6 : 0;
        for (int row = 0; row < 7; ++row) {
            const int item = first + row;
            const int y = 12 + row * 7;
            if (item == 10) {
                gfxPrint(1, y, "RANDOM");
                gfxPrint(38, y, "PUSH");
            } else if (item >= 8) {
                gfxPrint(1, y, item == 8 ? "CV1" : "CV2");
                gfxPrint(26, y, mode_name(cv_mode_[item - 8]));
            } else {
                gfxPrint(1, y, labels[item]);
                gfxPrint(47, y, parameter(item));
            }
        }
        gfxCursor(0, 19 + (cursor_ - first) * 7, 63, 7);
    }

    void OnButtonPress() {
        if (cursor_ == 10) {
            for (int i = 0; i < 7; ++i) parameter(i) = random(64);
            dsp_.UpdateControls();
        } else CursorToggle();
    }

    void OnEncoderMove(int direction) {
        if (!EditMode()) {
            MoveCursor(cursor_, direction, 10);
            return;
        }
        if (cursor_ == 10) return;
        if (cursor_ >= 8) {
            uint8_t &mode = cv_mode_[cursor_ - 8];
            // Menu order differs from stable preset/DSP IDs. Keep old saves intact.
            static const uint8_t menu_order[RivetDSP::CV_TARGET_COUNT] = {
                RivetDSP::CV_OFF, RivetDSP::CV_PITCH, RivetDSP::CV_TIMBRE,
                RivetDSP::CV_ATTACK, RivetDSP::CV_IMPACT, RivetDSP::CV_RATTLE,
                RivetDSP::CV_CRUNCH, RivetDSP::CV_AIR, RivetDSP::CV_BODY,
                RivetDSP::CV_METAL, RivetDSP::CV_FOLD, RivetDSP::CV_NOISE,
                RivetDSP::CV_DECAY, RivetDSP::CV_SPREAD, RivetDSP::CV_SWEEP,
                RivetDSP::CV_LPF, RivetDSP::CV_METAL_FOLD,
                RivetDSP::CV_FOLD_NOISE, RivetDSP::CV_NOISE_DECAY,
                RivetDSP::CV_DECAY_SWEEP, RivetDSP::CV_SPREAD_METAL,
                RivetDSP::CV_FOLD_LPF
            };
            int position = 0;
            while (position < RivetDSP::CV_TARGET_COUNT && menu_order[position] != mode)
                ++position;
            position = constrain(position + direction, 0,
                                 (int)RivetDSP::CV_TARGET_COUNT - 1);
            mode = menu_order[position];
            dsp_.SetModes(cv_mode_[0], cv_mode_[1]);
            dsp_.UpdateControls();
            return;
        }
        uint8_t &value = parameter(cursor_);
        value = constrain((int)value + direction, 0, 63);
        dsp_.UpdateControls();
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        for (int i = 0; i < 8; ++i)
            Pack(data, PackLocation{(uint8_t)(i * 6), 6}, parameter(i));
        Pack(data, PackLocation{48, 5}, cv_mode_[0]);
        Pack(data, PackLocation{53, 5}, cv_mode_[1]);
        Pack(data, PackLocation{61, 1}, 1); // five-bit CV mode format
        Pack(data, PackLocation{62, 1}, 1); // CV-assignment format marker
        Pack(data, PackLocation{63, 1}, 1); // format marker for older presets
        return data;
    }

    void OnDataReceive(uint64_t data) {
        for (int i = 0; i < 7; ++i)
            parameter(i) = constrain(Unpack(data, PackLocation{(uint8_t)(i * 6), 6}), 0, 63);
        parameter(7) = Unpack(data, PackLocation{63, 1})
            ? Unpack(data, PackLocation{42, 6}) : 20;
        if (Unpack(data, PackLocation{61, 1})) {
            cv_mode_[0] = constrain(Unpack(data, PackLocation{48, 5}), 0,
                                    (int)RivetDSP::CV_TARGET_COUNT - 1);
            cv_mode_[1] = constrain(Unpack(data, PackLocation{53, 5}), 0,
                                    (int)RivetDSP::CV_TARGET_COUNT - 1);
        } else if (Unpack(data, PackLocation{62, 1})) {
            // Original CV assignment presets used adjacent four-bit fields.
            cv_mode_[0] = Unpack(data, PackLocation{48, 4});
            cv_mode_[1] = Unpack(data, PackLocation{52, 4});
        } else {
            cv_mode_[0] = RivetDSP::CV_PITCH;
            cv_mode_[1] = RivetDSP::CV_TIMBRE;
        }
        dsp_.SetModes(cv_mode_[0], cv_mode_[1]);
        dsp_.UpdateControls();
    }

protected:
    void SetHelp() {
        help[HELP_DIGITAL1] = "Hit";
        help[HELP_DIGITAL2] = "Accent";
        help[HELP_CV1] = mode_name(cv_mode_[0]);
        help[HELP_CV2] = mode_name(cv_mode_[1]);
        help[HELP_OUT1] = "Audio";
        help[HELP_OUT2] = "Env";
        help[HELP_EXTRA1] = "Press: edit/RND";
        help[HELP_EXTRA2] = "Turn: select/value";
    }

private:
    static const char* mode_name(uint8_t mode) {
        static const char* names[RivetDSP::CV_TARGET_COUNT] = {
            "Off", "1V/oct", "Timbre", "Metal", "Fold",
            "Noise", "Decay", "Spread", "Sweep", "LPF",
            "M+F", "F+N", "N+D", "D+Sw", "Sp+M", "F+LPF",
            "Attack", "Impact", "Rattle", "Crunch", "Air", "Body"
        };
        return names[mode < RivetDSP::CV_TARGET_COUNT ? mode : 0];
    }

    uint8_t &parameter(int i) {
        switch (i) {
        case 0: return dsp_.settings.pitch;
        case 1: return dsp_.settings.decay;
        case 2: return dsp_.settings.spread;
        case 3: return dsp_.settings.metal;
        case 4: return dsp_.settings.fold;
        case 5: return dsp_.settings.noise;
        case 6: return dsp_.settings.sweep;
        default: return dsp_.settings.cutoff;
        }
    }

    RivetDSP dsp_;
    uint8_t cv_mode_[2] = {RivetDSP::CV_PITCH, RivetDSP::CV_TIMBRE};
    int cursor_ = 0;
    uint8_t control_tick_ = 0;
};
