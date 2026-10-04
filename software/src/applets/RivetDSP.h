#pragma once

#include <stdint.h>

// Fixed-point, mono percussion engine for the 16,666 Hz O_C core tick.
// No allocation, floating point, or division in Render().
class RivetDSP {
public:
    static constexpr uint32_t kRate = 16666;
    static constexpr int kMax = 63;
    enum CVTarget : uint8_t {
        CV_OFF, CV_PITCH, CV_TIMBRE, CV_METAL, CV_FOLD,
        CV_NOISE, CV_DECAY, CV_SPREAD, CV_SWEEP, CV_LPF,
        CV_METAL_FOLD, CV_FOLD_NOISE, CV_NOISE_DECAY,
        CV_DECAY_SWEEP, CV_SPREAD_METAL, CV_FOLD_LPF,
        CV_ATTACK, CV_IMPACT, CV_RATTLE, CV_CRUNCH,
        CV_AIR, CV_BODY, CV_TARGET_COUNT
    };
    static_assert(CV_TARGET_COUNT <= 32, "CV target must fit preset's 5-bit field");
    struct Settings {
        uint8_t pitch = 20, decay = 35, spread = 26, metal = 24;
        uint8_t fold = 20, noise = 14, sweep = 28, cutoff = 20;
    } settings;

    void Trigger(bool accent) {
        // Finish the old hit over 16 ticks before restarting phase/envelopes.
        pending_accent_ = accent;
        fade_ = 16;
    }

    void Silence() {
        master_ = noise_env_ = pitch_env_ = 0;
        for (int i = 0; i < 3; ++i) env_[i] = 0;
        last_audio_ = last_env_ = filter_ = 0;
        fade_ = attack_ = 0;
    }

    void SetModes(uint8_t cv1, uint8_t cv2) {
        cv_mode_[0] = cv1 < CV_TARGET_COUNT ? cv1 : CV_OFF;
        cv_mode_[1] = cv2 < CV_TARGET_COUNT ? cv2 : CV_OFF;
    }

    void SetCV(int cv1, int cv2) {
        // CV units are 128 per semitone. One volt is 1,536 units.
        cv_[0] = clamp(cv1, -6144, 6144);
        cv_[1] = clamp(cv2, -6144, 6144);
    }

    // Assignments are additive; both CVs may address the same destination.
    // Call this at ADC/control rate, never from the audio sample path.
    void UpdateControls() {
        int pitch_cv = 0, macro = 0, metal_cv = 0, fold_cv = 0;
        int noise_cv = 0, decay_cv = 0, spread_cv = 0, sweep_cv = 0, lpf_cv = 0;
        for (int i = 0; i < 2; ++i) {
            const int cv = cv_[i];
            switch (cv_mode_[i]) {
            case CV_PITCH: pitch_cv += cv; break;
            case CV_TIMBRE: macro += cv / 48; break;
            case CV_METAL: metal_cv += cv / 48; break;
            case CV_FOLD: fold_cv += cv / 48; break;
            case CV_NOISE: noise_cv += cv / 48; break;
            case CV_DECAY: decay_cv += cv / 96; break;
            case CV_SPREAD: spread_cv += cv / 96; break;
            case CV_SWEEP: sweep_cv += cv / 96; break;
            case CV_LPF: lpf_cv += cv / 96; break;
            case CV_METAL_FOLD:
                metal_cv += cv / 48; fold_cv += cv / 48; break;
            case CV_FOLD_NOISE:
                fold_cv += cv / 48; noise_cv += cv / 48; break;
            case CV_NOISE_DECAY:
                noise_cv += cv / 48; decay_cv += cv / 96; break;
            case CV_DECAY_SWEEP:
                decay_cv += cv / 96; sweep_cv += cv / 96; break;
            case CV_SPREAD_METAL:
                spread_cv += cv / 96; metal_cv += cv / 48; break;
            case CV_FOLD_LPF:
                fold_cv += cv / 48; lpf_cv += cv / 96; break;
            case CV_ATTACK:
                noise_cv += cv / 48; sweep_cv += cv / 96;
                metal_cv += cv / 48; break;
            case CV_IMPACT:
                decay_cv += cv / 96; sweep_cv += cv / 96;
                fold_cv += cv / 48; break;
            case CV_RATTLE:
                spread_cv += cv / 96; metal_cv += cv / 48;
                noise_cv += cv / 48; break;
            case CV_CRUNCH:
                fold_cv += cv / 48; metal_cv += cv / 48;
                lpf_cv += cv / 96; break;
            case CV_AIR:
                noise_cv += cv / 48; fold_cv += cv / 48;
                lpf_cv += cv / 96; break;
            case CV_BODY:
                decay_cv += cv / 96; spread_cv += cv / 96;
                fold_cv += cv / 48; break;
            default: break;
            }
        }
        pitch_cv = clamp(pitch_cv, -3072, 6144);
        metal_ = clamp(settings.metal * 4 + macro + metal_cv, 0, 252);
        fold_ = clamp(settings.fold * 4 + macro + fold_cv, 0, 252);
        noise_ = clamp(settings.noise * 4 + macro / 2 + noise_cv, 0, 252);
        noise_length_ = clamp(settings.noise + noise_cv / 4, 0, 63);
        decay_ = clamp(settings.decay + decay_cv, 0, 63);
        spread_ = clamp(settings.spread + spread_cv, 0, 63);
        sweep_ = clamp(settings.sweep + sweep_cv, 0, 63);
        // 20 preserves the original one-pole coefficient of 64/256.
        filter_coeff_ = 4 + clamp(settings.cutoff + lpf_cv, 0, 63) * 3;

        // Linear interpolation within an octave gives continuous 1 V/oct.
        int cv = pitch_cv;
        int oct = cv / 1536;
        int rem = cv % 1536;
        if (rem < 0) { rem += 1536; --oct; }
        const int index = rem / 12; // 128 steps/octave, interpolated below
        uint32_t ratio = octave_ratio(index);
        ratio += (octave_ratio(index + 1) - ratio) * (rem % 12) / 12;
        uint64_t increment = ((uint64_t)(35 + settings.pitch * 5) * ratio
                            * (4294967296ULL / kRate)) >> 15;
        if (oct > 0) increment <<= oct;
        else if (oct < 0) increment >>= -oct;
        const uint32_t min_inc = 20ULL * 4294967296ULL / kRate;
        const uint32_t max_base_inc = 1300ULL * 4294967296ULL / kRate;
        if (increment < min_inc) increment = min_inc;
        if (increment > max_base_inc) increment = max_base_inc;
        base_inc_ = increment;

        // 1.0x .. 2.4x; cap every partial below 4 kHz (Nyquist is 8.3 kHz).
        uint32_t ratio2 = 64 + spread_;
        uint32_t ratio3 = 64 + 2 * spread_;
        inc_[0] = base_inc_;
        inc_[1] = ((uint64_t)base_inc_ * ratio2) >> 6;
        inc_[2] = ((uint64_t)base_inc_ * ratio3) >> 6;
        const uint32_t max_inc = 3000ULL * 4294967296ULL / kRate;
        for (int i = 1; i < 3; ++i) if (inc_[i] > max_inc) inc_[i] = max_inc;
    }

    // Signed audio CV in O_C pitch units (about +/-2 V), and 0..5 V envelope.
    int Render(int &envelope_cv) {
        if (fade_) {
            --fade_;
            int result = last_audio_ * fade_ / 16;
            envelope_cv = (last_env_ * fade_) / 16;
            if (!fade_) StartHit();
            return result;
        }
        if (attack_) {
            master_ += 2048;
            if (!--attack_) master_ = 32767;
        }
        if (!master_) {
            envelope_cv = 0;
            last_audio_ = last_env_ = 0;
            return 0;
        }

        uint32_t extra = ((uint64_t)inc_[0] * sweep_ * pitch_env_) >> 21;
        phase_[0] += inc_[0] + extra;
        phase_[1] += inc_[1] + extra / 2;
        phase_[2] += inc_[2] + extra / 3;

        int32_t mod = sine(phase_[1]);
        int32_t offset = mod * metal_ * 256; // at most ~1.6 radians
        int32_t a = sine(phase_[0] + offset);
        int32_t b = mod;
        int32_t c = sine(phase_[2] - offset / 2);
        int32_t signal = (a * env_[0] / 32768 + b * env_[1] / 32768
                        + c * env_[2] / 32768) / 3;

        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        int32_t white = (int16_t)(rng_ >> 16);
        // High-pass noise to avoid a wandering DC component.
        noise_lp_ += (white - noise_lp_) >> 4;
        signal += (((white - noise_lp_) * noise_) >> 8) * noise_env_ / 32768 / 4;

        // Symmetric reflection fold, bounded before the final envelope.
        signal = (signal * (256 + fold_ * 4)) >> 8;
        for (int i = 0; i < 6; ++i) {
            if (signal > 16384) signal = 32768 - signal;
            else if (signal < -16384) signal = -32768 - signal;
        }
        if (signal > 16384) signal = 16384;
        if (signal < -16384) signal = -16384;
        // Adjustable post-fold low-pass. Coefficient is cached at control rate.
        filter_ += ((signal - filter_) * filter_coeff_) >> 8;
        int32_t out = filter_ * master_ / 32768;
        if (accent_) out = out * 5 / 4;
        out = out * 3072 / 16384; // +/-2 V maximum in O_C pitch units
        if (out > 3072) out = 3072;
        if (out < -3072) out = -3072;
        last_audio_ = out;
        envelope_cv = master_ * 5 * 1536 / 32768;
        last_env_ = envelope_cv;

        for (int i = 0; i < 3; ++i) decay(env_[i], env_step_[i]);
        if (!attack_) decay(master_, master_step_);
        decay(noise_env_, noise_step_);
        decay(pitch_env_, pitch_step_);
        return out;
    }

    bool Active() const { return master_ || fade_; }
    uint32_t BaseIncrement() const { return base_inc_; }

private:
    static int clamp(int x, int low, int high) {
        return x < low ? low : x > high ? high : x;
    }
    static void decay(int32_t &x, int32_t step) {
        x = x > step ? x - step : 0;
    }
    static uint32_t octave_ratio(int index) {
        // Q15 ratios for 2^(index/128), using a 16-point linear table.
        static const uint16_t table[17] = {
            32768,34217,35734,37316,38968,40693,42495,44376,
            46341,48394,50535,52770,55109,57549,60097,62757,65535
        };
        if (index >= 128) return 65535;
        int segment = index >> 3;
        int fraction = index & 7;
        return table[segment] + ((int32_t)(table[segment+1] - table[segment]) * fraction >> 3);
    }
    static int32_t point(uint8_t i) {
        static const int16_t quarter[17] = {
            0,1606,3196,4756,6270,7723,9102,10394,11585,
            12665,13623,14449,15137,15679,16069,16305,16384
        };
        i &= 63;
        uint8_t quadrant = i >> 4, pos = i & 15;
        if (quadrant == 0) return quarter[pos];
        if (quadrant == 1) return quarter[16-pos];
        if (quadrant == 2) return -quarter[pos];
        return -quarter[16-pos];
    }
    static int32_t sine(uint32_t phase) {
        uint8_t index = phase >> 26;
        uint16_t fraction = (phase >> 16) & 1023;
        int32_t a = point(index);
        return a + ((point(index + 1) - a) * fraction >> 10);
    }
    static int32_t step_for_ms(int ms) {
        int32_t step = (32767 * 1000L) / (ms * (int32_t)kRate);
        return step > 0 ? step : 1;
    }
    void StartHit() {
        accent_ = pending_accent_;
        for (int i = 0; i < 3; ++i) phase_[i] = 0;
        // 1 ms attack ramp prevents a hard DAC discontinuity.
        master_ = 0;
        attack_ = 16;
        for (int i = 0; i < 3; ++i) env_[i] = 32767;
        noise_env_ = pitch_env_ = 32767;
        int ms = 35 + decay_ * 15;
        master_step_ = step_for_ms(ms);
        env_step_[0] = step_for_ms(ms);
        env_step_[1] = step_for_ms(ms * 2 / 3);
        env_step_[2] = step_for_ms(ms / 2);
        noise_step_ = step_for_ms(8 + noise_length_ * 2);
        pitch_step_ = step_for_ms(12 + sweep_);
    }

    int32_t master_ = 0, env_[3] = {}, noise_env_ = 0, pitch_env_ = 0;
    int32_t master_step_ = 1, env_step_[3] = {1,1,1}, noise_step_ = 1, pitch_step_ = 1;
    uint32_t phase_[3] = {}, inc_[3] = {}, base_inc_ = 0;
    uint32_t rng_ = 0x8fe642a1;
    int32_t noise_lp_ = 0, filter_ = 0;
    int32_t last_audio_ = 0, last_env_ = 0;
    int cv_[2] = {};
    uint8_t cv_mode_[2] = {CV_PITCH, CV_TIMBRE};
    int metal_ = 0, fold_ = 0, noise_ = 0, filter_coeff_ = 64;
    int decay_ = 35, spread_ = 26, sweep_ = 28, noise_length_ = 14;
    uint8_t fade_ = 0, attack_ = 0;
    bool pending_accent_ = false, accent_ = false;
};
