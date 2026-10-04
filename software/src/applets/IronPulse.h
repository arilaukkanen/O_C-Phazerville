#pragma once

#include "IronPulseDSP.h"

// A compact, BIA-inspired percussion voice for ordinary T32 DAC outputs.
class IronPulse : public HemisphereApplet {
public:
    const char* applet_name() { return "IronPulse"; }

    void Start() {
        cursor_ = 0;
        control_tick_ = 0;
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
        static const char* labels[7] = {
            "Pitch", "Decay", "Spread", "Metal", "Fold", "Noise", "Sweep"
        };
        for (int i = 0; i < 7; ++i) {
            const int y = 12 + i * 7;
            gfxPrint(1, y, labels[i]);
            gfxPrint(47, y, parameter(i));
        }
        gfxCursor(0, 19 + cursor_ * 7, 63, 7);
    }

    void OnEncoderMove(int direction) {
        if (!EditMode()) {
            MoveCursor(cursor_, direction, 6);
            return;
        }
        uint8_t &value = parameter(cursor_);
        value = constrain((int)value + direction, 0, 63);
        dsp_.UpdateControls();
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        for (int i = 0; i < 7; ++i)
            Pack(data, PackLocation{(uint8_t)(i * 6), 6}, parameter(i));
        return data;
    }

    void OnDataReceive(uint64_t data) {
        for (int i = 0; i < 7; ++i)
            parameter(i) = constrain(Unpack(data, PackLocation{(uint8_t)(i * 6), 6}), 0, 63);
        dsp_.UpdateControls();
    }

protected:
    void SetHelp() {
        help[HELP_DIGITAL1] = "Hit";
        help[HELP_DIGITAL2] = "Accent";
        help[HELP_CV1] = "1V/oct";
        help[HELP_CV2] = "Timbre";
        help[HELP_OUT1] = "Audio";
        help[HELP_OUT2] = "Env";
        help[HELP_EXTRA1] = "Press: edit";
        help[HELP_EXTRA2] = "Turn: parameter";
    }

private:
    uint8_t &parameter(int i) {
        switch (i) {
        case 0: return dsp_.settings.pitch;
        case 1: return dsp_.settings.decay;
        case 2: return dsp_.settings.spread;
        case 3: return dsp_.settings.metal;
        case 4: return dsp_.settings.fold;
        case 5: return dsp_.settings.noise;
        default: return dsp_.settings.sweep;
        }
    }

    IronPulseDSP dsp_;
    int cursor_ = 0;
    uint8_t control_tick_ = 0;
};
