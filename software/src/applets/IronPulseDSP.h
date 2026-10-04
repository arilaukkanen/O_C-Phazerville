#pragma once

#include <stdint.h>

// Fixed-point, mono percussion engine for the 16,666 Hz O_C core tick.
// No allocation, floating point, or division in Render().
class IronPulseDSP {
public:
    static constexpr uint32_t kRate = 16666;
    static constexpr int kMax = 63;
    struct Settings {
        uint8_t pitch = 20, decay = 35, spread = 26, metal = 24;
        uint8_t fold = 20, noise = 14, sweep = 28;
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

    void SetCV(int pitch_cv, int timbre_cv) {
        // CV units are 128 per semitone. One volt is 1,536 units.
        if (pitch_cv < -3072) pitch_cv = -3072;
        if (pitch_cv > 6144) pitch_cv = 6144;
        if (timbre_cv < -6144) timbre_cv = -6144;
        if (timbre_cv > 6144) timbre_cv = 6144;
        pitch_cv_ = pitch_cv;
        timbre_cv_ = timbre_cv;
    }

    // CV2 is a bipolar Metal/Fold/Noise macro. Call at ADC/control rate.
    void UpdateControls() {
        int macro = timbre_cv_ / 48;
        metal_ = clamp(settings.metal * 4 + macro, 0, 252);
        fold_ = clamp(settings.fold * 4 + macro, 0, 252);
        noise_ = clamp(settings.noise * 4 + macro / 2, 0, 252);

        // Linear interpolation within an octave gives continuous 1 V/oct.
        int cv = pitch_cv_;
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
        uint32_t ratio2 = 64 + settings.spread;
        uint32_t ratio3 = 64 + 2 * settings.spread;
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

        uint32_t extra = ((uint64_t)inc_[0] * settings.sweep * pitch_env_) >> 21;
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
        // Gentle post-fold low-pass attenuates the highest aliased products.
        filter_ += (signal - filter_) >> 2;
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
        int ms = 35 + settings.decay * 15;
        master_step_ = step_for_ms(ms);
        env_step_[0] = step_for_ms(ms);
        env_step_[1] = step_for_ms(ms * 2 / 3);
        env_step_[2] = step_for_ms(ms / 2);
        noise_step_ = step_for_ms(8 + settings.noise * 2);
        pitch_step_ = step_for_ms(12 + settings.sweep);
    }

    int32_t master_ = 0, env_[3] = {}, noise_env_ = 0, pitch_env_ = 0;
    int32_t master_step_ = 1, env_step_[3] = {1,1,1}, noise_step_ = 1, pitch_step_ = 1;
    uint32_t phase_[3] = {}, inc_[3] = {}, base_inc_ = 0;
    uint32_t rng_ = 0x8fe642a1;
    int32_t noise_lp_ = 0, filter_ = 0;
    int32_t last_audio_ = 0, last_env_ = 0;
    int pitch_cv_ = 0, timbre_cv_ = 0;
    int metal_ = 0, fold_ = 0, noise_ = 0;
    uint8_t fade_ = 0, attack_ = 0;
    bool pending_accent_ = false, accent_ = false;
};
