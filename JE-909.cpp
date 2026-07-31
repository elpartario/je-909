#include "daisy_patch_sm.h"
#include "daisysp.h"
#include <cmath>
#include <cstdint>


using namespace daisy;
using namespace patch_sm;
using namespace daisysp;

DaisyPatchSM hw;
Switch button; // B7 Kick mute
Switch toggle; // B8 Sidechain on/off

// ------------------------------------------------------------
// Constants
// ------------------------------------------------------------

static constexpr float TWO_PI = 6.28318530718f;

// CV_5 trigger detector thresholds.
// These are intentionally conservative.
// If trigger is not detected, lower TRIG_HIGH.
// If it double-triggers, reaise TRIG_HIGH or raise TRIG_LOW slightly.
static constexpr float TRIG_HIGH = 0.25f;
static constexpr float TRIG_LOW  = 0.10f;

// ------------------------------------------------------------
// Utility functions
// ------------------------------------------------------------

static inline float Clamp(float x, float lo, float hi)
{
	if (x < lo) return lo;
	if (x > hi) return hi;
	return x;
}

static inline float MapLin(float x, float out_min, float out_max)
{
	x = Clamp(x, 0.0f, 1.0f);
	return out_min + x * (out_max - out_min);
}

static inline float MapExp(float x, float out_min, float out_max)
{
	x = Clamp(x, 0.0f, 1.0f);

	// Exponential mapping is better for frequency and time
    // because our ears do not perceive those linearly.
	return out_min * powf(out_max / out_min, x);
}

static inline float OnePoleCoeff(float cutoff_hz, float sample_rate)
{
	cutoff_hz = Clamp(cutoff_hz, 1.0f, sample_rate * 0.45f);
	return 1.0f - expf(-TWO_PI * cutoff_hz / sample_rate);
}

// ------------------------------------------------------------
// Exponential decay envelope
// This models the behavior of a capacitor discharging through
// a resistor: V(t) = V0 * e^(-t / tau)
// ------------------------------------------------------------

struct ExpDecayEnv
{
	float sample_rate = 48000.0f;
	float value = 0.0f;
	float coeff = 0.999f;

	void Init(float sr, float decay_seconds)
	{
		sample_rate = sr;
		SetDecay(decay_seconds);
		value = 0.0f;
	}

	void SetDecay(float decay_seconds)
	{
		decay_seconds = Clamp(decay_seconds, 0.0001f, 10.0f);
		coeff = expf(-1.0f / (decay_seconds * sample_rate));
	}

	void Trigger()
	{
		value = 1.0f;
	}

	float Process()
	{
		float out = value;

		value *= coeff;

		if(value < 0.000001f)
			value = 0.0f;
		return out;
	}
};

// ------------------------------------------------------------
// One-pole low-pass filter
// Digital equivalent of a simple RC low-pass filter.
// ------------------------------------------------------------

struct OnePoleLPF
{
	float sample_rate = 48000.0f;
	float z = 0.0f;
	float a = 0.1f;

	void Init(float sr, float cutoff_hz)
	{ 
		sample_rate = sr;
		z = 0.0f;
		SetCutoff(cutoff_hz);
	}

	void SetCutoff(float cutoff_hz)
	{ 
		a = OnePoleCoeff(cutoff_hz, sample_rate);
	}

	float Process(float x)
	{
		z += a * (x - z);
		return z;
	}

};

// ------------------------------------------------------------
// Biquad band-pass filter
// This is used to model the band-pass part of the 909 pulse
// shaping circuit.
// ------------------------------------------------------------

struct BiquadBandpass
{
	float sample_rate = 48000.0f;

	float b0 = 0.0f;
	float b1 = 0.0f;
	float b2 = 0.0f;
	float a1 = 0.0f;
	float a2 = 0.0f;

	float x1 = 0.0f;
	float x2 = 0.0f;
	float y1 = 0.0f;
	float y2 = 0.0f;

	void Init(float sr, float cutoff_hz, float q)
	{
		sample_rate = sr;
		x1 = x2 = y1 = y2 = 0.0f;
		Set(cutoff_hz, q);
	}

	void Set(float cutoff_hz, float q)
	{
		cutoff_hz = Clamp(cutoff_hz, 20.0f, sample_rate * 0.45f);
		q = Clamp(q, 0.1f, 10.0f);

		float omega = TWO_PI * cutoff_hz / sample_rate;
		float sn = sinf(omega);
		float cs = cosf(omega);
		float alpha = sn / (2.0f * q);

		float raw_b0 = alpha;
		float raw_b1 = 0.0f;
		float raw_b2 = -alpha;
		float raw_a0 = 1.0f + alpha;
		float raw_a1 = -2.0f * cs;
		float raw_a2 = 1.0f - alpha;

		b0 = raw_b0 / raw_a0;
        b1 = raw_b1 / raw_a0;
        b2 = raw_b2 / raw_a0;
        a1 = raw_a1 / raw_a0;
        a2 = raw_a2 / raw_a0;
	}

	float Process (float x)
	{
		float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

		x2 = x1;
		x1 = x;

		y2 = y1;
		y1 = y;

		return y;
	}
};

// ------------------------------------------------------------
// DC blocker
// Removes offset from asymmetric saturation.
// Similar in purpose to AC coupling at an analog output.
// ------------------------------------------------------------

struct DCBlocker
{
	float x1 = 0.0f;
	float y1 = 0.0f;
	float R = 0.995f;

	void Init()
	{
		x1 = 0.0f;
		y1 = 0.0f;
	}

	float Process(float x)
	{
		float y = x - x1 + R * y1;

		x1 = x;
		y1 = y;

		return y;
	}
};

struct Kick909
{
	float sample_rate = 48000.0f;

	float phase = 0.0f;
	float base_freq = 50.f;
	float drive_amount = 0.0f;
	float level = 0.8f;

	ExpDecayEnv env1_body;   // ENV-1: body amplitude, responsible for punch
    ExpDecayEnv env2_attack; // ENV-2: attack VCA envelope
    ExpDecayEnv env3_pitch;  // ENV-3: pitch modulation envelope
    ExpDecayEnv pulse_env;   // helper envelope for pulse-generator behavior

    OnePoleLPF noise_lpf;    // low-pass filtered noise path
    OnePoleLPF pulse_lpf;    // low-pass part of pulse shaping
    BiquadBandpass pulse_bpf; // band-pass part of pulse shaping
    OnePoleLPF output_lpf;   // final output smoothing
    DCBlocker dc_blocker;

	uint32_t rng = 1;

	void Init(float sr)
	{
		sample_rate = sr;
		phase = 0.0f;

		env1_body.Init(sample_rate, 0.45f);
		env2_attack.Init(sample_rate, 0.014f);
		env3_pitch.Init(sample_rate, 0.035f);
		pulse_env.Init(sample_rate, 0.0011f);

		noise_lpf.Init(sample_rate, 6500.0f);
		pulse_lpf.Init(sample_rate, 8500.0f);
		pulse_bpf.Init(sample_rate, 4800.0f, 0.85f);
		output_lpf.Init(sample_rate, 14000.0f);

		dc_blocker.Init();

		rng = 1;
	}

	void SetParrams(float tune_knob, float decay_knob, float drive_knob, float level_knob)
	{
		base_freq = MapExp(tune_knob, 32.70f, 65.41f);

		float decay_seconds = MapExp(decay_knob, 0.10f, 1.60f);
		env1_body.SetDecay(decay_seconds);

		env2_attack.SetDecay(0.014f);
		env3_pitch.SetDecay(0.035f);
		pulse_env.SetDecay(0.0011f);

		drive_amount = Clamp(drive_knob, 0.0f, 1.0f);

		level = level_knob * 1.25f;
	}

	void Trigger()
	{
		env1_body.Trigger();
		env2_attack.Trigger();
		env3_pitch.Trigger();
		pulse_env.Trigger();

		phase = 0.0f;
	}

	float WhiteNoise()
	{
		rng = 1664525UL * rng + 1013904223UL;

		uint32_t bits = (rng >> 9) | 0x3f800000UL;
		
		union 
		{
			uint32_t i;
			float f;
		} u;

		u.i = bits;

		float uni = u.f - 1.0f;

		return uni * 2.0f - 1.0f;
	}

	float DiodeShape(float x)
	{
		float shape_gain = 1.85f;

		float y = tanhf(x * shape_gain);

		y /= tanhf(shape_gain);

		return y;
	}

	float Drive(float x)
	{ 
		float d = drive_amount;

		float pregain = 1.0f + 24.0f * d * d;
		float bias = 0.08f * d;

		float saturated = tanhf((x * pregain) + bias) - tanhf(bias);

		float norm = tanhf(pregain);

		if(norm > 0.0001f)
			saturated /= norm;

		float y = (1.0f - d) * x + d * saturated;

		return y;
	}

	float Process()
	{
	    // ------------------------------------------------------------
        // ENV-1, ENV-2, ENV-3
        // ------------------------------------------------------------

		float env1 = env1_body.Process();
		float env2 = env2_attack.Process();
		float env3 = env3_pitch.Process();

		float pulse_e = pulse_env.Process();

        // ------------------------------------------------------------
        // Control Voltage Generator + VCO
        // ENV-3 modulates pitch. Tune sets the base pitch.
        // ------------------------------------------------------------
		
		float pitch_sweep_octaves = 1.65f;

		float freq = base_freq * powf(2.0f, pitch_sweep_octaves * env3);

		freq = Clamp(freq, 10.0f, sample_rate * 0.40f);

		phase += freq / sample_rate;

		if(phase >= 1.0f)
			phase -= 1.0f;

		// ------------------------------------------------------------
        // VCO body path
        // Original analysis: VCO triangle passes diode clipper
        // to become sine-like.
        // ------------------------------------------------------------

		float tri = 1.0f - 4.0f * fabsf(phase - 0.5f);

		float body_wave = DiodeShape(tri);

		// ENV-1 is described as punchy/compressed.
        // Power < 1.0 keeps the envelope stronger near the front.
		float env1_punch = powf(env1, 0.65f);

		float body = body_wave * env1_punch;

		// ------------------------------------------------------------
        // Attack path: pulse + noise
        // Pulse is shaped by low-pass and band-pass filtering.
        // Noise is low-pass filtered.
        // Both pass through VCA controlled by ENV-2.
        // ------------------------------------------------------------

		float pulse_lp = pulse_lpf.Process(pulse_e);
		float pulse_bp = pulse_bpf.Process(pulse_e);
		float pulse = (0.62f * pulse_lp) + (0.38f * pulse_bp);

		float raw_noise = WhiteNoise();
		float filtered_noise = noise_lpf.Process(raw_noise);

		float attack = ((0.90f * pulse) + (0.22f * filtered_noise)) * env2;

        // ------------------------------------------------------------
        // Final summing and output stage
        // ------------------------------------------------------------

		float summed = (0.95f * body) + (0.75f * attack);

		float driven = Drive(summed);

		float filtered = output_lpf.Process(driven);

		float out = dc_blocker.Process(filtered);

		return out * level;
	}
};

Kick909 kick;

// ------------------------------------------------------------
// Sidechain duck envelope
// ------------------------------------------------------------

float duck_env = 0.0f;
float duck_coeff = 0.999f;

void TriggerDuck()
{
	duck_env = 1.0f;
}

float ProcessDuck()
{
	float out = duck_env;

	duck_env *= duck_coeff;

	if(duck_env < 0.000001f)
		duck_env = 0.0f;

	return out;
}

// ------------------------------------------------------------
// Global state
// ------------------------------------------------------------

bool kick_muted = false;
bool trig_was_high = false;

// ------------------------------------------------------------
// Audio callback
// -----------------------------------------------------------

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    // Update all ADC and digital controls once per audio block.    
	hw.ProcessAllControls();

    // Debounce button/switch states.
    button.Debounce();
    toggle.Debounce();

    // ------------------------------------------------------------
    // Read front-panel controls
    // ------------------------------------------------------------

	float tune_knob = hw.GetAdcValue(CV_1);
	float decay_knob = hw.GetAdcValue(CV_2);
	float drive_knob = hw.GetAdcValue(CV_3);
	float level_knob = hw.GetAdcValue(CV_4);

	kick.SetParrams(tune_knob, decay_knob, drive_knob, level_knob);

	if(button.RisingEdge())
	{
		kick_muted = !kick_muted;
	}

	// Optional LED feedback:
    // LED on means kick is NOT muted.
	hw.SetLed(!kick_muted);

	// ------------------------------------------------------------
    // B8: sidechain on/off
    // ------------------------------------------------------------

	bool sidechain_on = toggle.Pressed();

	// ------------------------------------------------------------
    // CV_5: exclusive kick trigger input
    // ------------------------------------------------------------

	float trig_cv = hw.GetAdcValue(CV_5);

	bool trig_high = trig_was_high;

	if(!trig_was_high && trig_cv > TRIG_HIGH)
	{
		trig_high = true;

		kick.Trigger();
		TriggerDuck();
	}
	else if(trig_was_high && trig_cv < TRIG_LOW)
	{ 
		trig_high = false;
	}

	trig_was_high = trig_high;

    // ------------------------------------------------------------
    // Per-sample audio
    // ------------------------------------------------------------

	for (size_t i = 0; i < size; i++)
	{
		float dry_l = in[0][i];
		float dry_r = in[1][i];

		float duck = ProcessDuck();

		float input_gain = 1.0f;

		if(sidechain_on)
		{
			float duck_depth = 0.75f;
			input_gain = 1.0f - duck_depth * duck;
		}

		dry_l *= input_gain;
		dry_r *= input_gain;

		float kick_sample = kick.Process();

		if(kick_muted)
			kick_sample = 0.0f;
		out[0][i] = dry_l + kick_sample;
		out[1][i] = dry_r + kick_sample;
	}
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main(void)
{
	hw.Init();
	hw.SetAudioBlockSize(4); // number of samples handled per callback
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
	float sample_rate = hw.AudioSampleRate();
	button.Init(DaisyPatchSM::B7, hw.AudioCallbackRate()); // Initializing B7 button as switch object, using audio callback rate for debounce timing.
	toggle.Init(DaisyPatchSM::B8, hw.AudioCallbackRate());
	kick.Init(sample_rate); // Initialize kick engine.
	float duck_release_seconds = 0.135f;
	duck_coeff = expf(-1.0f / (duck_release_seconds * sample_rate)); // Sidechain release time. Smaller = faster recovery | Larger = deeper/pumpier duck.
	hw.StartAdc();
	hw.StartAudio(AudioCallback);
	while(1) {}
}