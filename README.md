# Patch.Init 909/Bohm-Inspired Kick Machine

## A practical and technical guide to the circuit model, DSP, C++ code, performance design, modification, building, and flashing

**Target hardware:** Electrosmith patch.Init() / Daisy Patch SM architecture  
**Language:** C++ using libDaisy  
**Reference sample rate:** 48 kHz  
**Reference audio block size:** 4 samples  
**Trigger:** `CV_5`  
**Audio:** stereo pass-through plus a mono kick mixed equally into both outputs  
**Current reference revision:** corrected envelope trigger level and corrected B7/B8 initialization

---

## Contents

1. [What this patch is](#1-what-this-patch-is)
2. [Design lineage: TR-909 and Bohm](#2-design-lineage-tr-909-and-bohm)
3. [What the patch does](#3-what-the-patch-does)
4. [Control and jack map](#4-control-and-jack-map)
5. [Performance concept](#5-performance-concept)
6. [The Daisy ecosystem](#6-the-daisy-ecosystem)
7. [Project structure in VS Code](#7-project-structure-in-vs-code)
8. [Building and flashing](#8-building-and-flashing)
9. [TR-909 bass drum circuit overview](#9-tr-909-bass-drum-circuit-overview)
10. [Digital signal-flow overview](#10-digital-signal-flow-overview)
11. [Complete reference code](#11-complete-reference-code)
12. [C++ anatomy for beginners](#12-c-anatomy-for-beginners)
13. [Detailed code walkthrough](#13-detailed-code-walkthrough)
14. [Mathematics and DSP foundations](#14-mathematics-and-dsp-foundations)
15. [Trigger, button, switch, mute, and sidechain logic](#15-trigger-button-switch-mute-and-sidechain-logic)
16. [How to modify the kick](#16-how-to-modify-the-kick)
17. [Patching and performance use](#17-patching-and-performance-use)
18. [Testing and calibration procedure](#18-testing-and-calibration-procedure)
19. [Troubleshooting](#19-troubleshooting)
20. [Possible future revisions](#20-possible-future-revisions)
21. [Glossary](#21-glossary)
22. [References](#22-references)

---

# 1. What this patch is

This project turns an Electrosmith patch.Init() into a dedicated, clock-triggered kick instrument. Its sound-generation architecture is based on a **functional interpretation of the Roland TR-909 bass drum circuit**, while its interface and performance priorities take inspiration from **Ohmforce Bohm and Bohm:Performer**.

The patch is not a sample player. It does not play a prerecorded 909 kick. It synthesizes every kick in real time using:

- an oscillator whose pitch falls after every trigger;
- three main envelope functions corresponding to the 909's ENV-1, ENV-2, and ENV-3 roles;
- a triangle-wave body shaped by a nonlinear function into a rounded, sine-like waveform;
- a separate pulse-and-noise attack section;
- low-pass and band-pass filtering;
- a body/attack summing stage;
- user-controlled drive;
- a kick-only output level control;
- stereo audio pass-through;
- optional trigger-derived ducking of the audio input;
- a latched kick mute.

The patch is best described as a **component-informed behavioral model**. It follows the major functional blocks of the 909 circuit, but it is not a transistor-by-transistor SPICE simulation. That distinction is important:

- A behavioral model asks, “What does this part of the circuit do to the sound?”
- A full circuit model asks, “What voltage and current exist at every resistor, capacitor, transistor, diode, and op-amp node?”

This project uses the first approach because it is computationally efficient, musically adjustable, and understandable enough to modify on embedded hardware.

---

# 2. Design lineage: TR-909 and Bohm

## 2.1 TR-909 influence

The central sound architecture comes from the TR-909 bass drum voice. The circuit analysis used for this project identifies two main sonic branches:

1. **Body or bottom-end path**
   - ENV-3 creates a pitch-control contour.
   - The pitch contour and TUNE setting control the VCO.
   - The VCO produces a triangle waveform.
   - A diode clipper reshapes the triangle into a rounded, sine-like body.
   - ENV-1 controls the body VCA and is especially important to the 909's punch.

2. **Attack path**
   - The trigger is shaped into a pulse by low-pass and band-pass networks.
   - A shared noise source is low-pass filtered.
   - Pulse and noise are mixed.
   - ENV-2 controls the attack VCA.

Finally, the body and attack are mixed and amplified by the output stage.

The code recreates that organization rather than simply generating a sine wave and calling it a 909.

## 2.2 Bohm influence

Bohm is a broader digital kick platform rather than one fixed analog voice. Its models can interpret controls differently, but its documentation describes several recurring concepts:

- a fundamental kick pitch, commonly spanning C1 to C2;
- a pitch curve;
- a primary oscillator/body;
- a transient synthesizer;
- duration and attack controls;
- post-effects;
- performance-oriented interaction;
- external audio input and trigger-derived ducking through the Performer system.

This Patch.Init project borrows Bohm's **performance logic**, not its internal model format. In particular:

- `CV_1` is a focused pitch control;
- `CV_2` is a focused decay control;
- `CV_3` is a drive control;
- `CV_4` is a kick-only level fader;
- B7 is a performative mute;
- B8 enables or bypasses input ducking;
- stereo audio can pass through the module while the kick is mixed into it.

The result is intentionally narrower than Bohm. The aim is not to reproduce Bohm's many model types, wavetables, snapshots, or secondary voices. The aim is to retain a small number of high-value controls that work quickly in a live modular or hybrid setup.

## 2.3 What “inspired by” means here

This project does **not** claim to be:

- an exact Roland circuit clone;
- an official Bohm emulation;
- a replacement for either device;
- a calibrated model of one specific vintage TR-909 unit.

It is a custom instrument whose architecture is informed by the 909 circuit and whose control layout is informed by Bohm's approach to playable kick design and performance routing.

---

# 3. What the patch does

The patch has five simultaneous jobs:

1. **Listen for a clock or trigger at `CV_5`.**
2. **Generate one synthesized kick for every new rising trigger.**
3. **Pass stereo audio input to stereo audio output.**
4. **Optionally duck that input whenever the kick triggers.**
5. **Provide immediate performance control over pitch, decay, drive, level, mute, and ducking.**

The kick is mono internally and is added equally to the left and right output channels. The input remains stereo.

The patch does not internally generate a rhythm. There is no sequencer in this firmware. The timing comes entirely from the external signal connected to `CV_5`. That makes the patch useful with:

- a dedicated Eurorack clock;
- Pamela's NEW Workout or a similar clock/modulation source;
- an Intellijel µMIDI or another MIDI-to-CV interface;
- a clock derived from a modular master clock such as Sir CLK;
- a gate pattern from a sequencer;
- a pulse generated by another Daisy, TouchDesigner-controlled rig, or MIDI/CV system.

---

# 4. Control and jack map

| Hardware control | Firmware role | What it changes |
|---|---|---|
| `CV_1` | Tune / pitch | Fundamental frequency of the kick body |
| `CV_2` | Decay | ENV-1/body duration |
| `CV_3` | Drive | Pre-gain, saturation, and clean/saturated blend |
| `CV_4` | Kick level | Final gain of the kick only |
| `CV_5` | Trigger input | A new rising pulse starts the kick |
| B7 | Latched mute | Press once to mute the kick; press again to unmute |
| B8 | Sidechain enable | Enables or bypasses ducking of the stereo input |
| Audio input L/R | Pass-through source | Stereo external signal to be passed and optionally ducked |
| Audio output L/R | Final output | Duckable stereo input plus mono kick |

## 4.1 Why CV_4 affects only the kick

The fourth control is deliberately placed after the internal kick synthesis. It does not turn down the external audio input. This means it behaves like a dedicated kick channel fader rather than a master-output control.

That distinction matters in performance. You can:

- fade the kick in and out without interrupting a sample player, synth bus, or DJ source passing through the module;
- leave sidechain pumping active while reducing the audible kick, if that is artistically useful;
- balance the kick against an external stereo source at the module itself;
- automate or manually ride kick level without changing the rest of the signal chain.

## 4.2 Why B7 is a latched mute

B7 changes a Boolean variable named `kick_muted`. It does not reset the oscillator parameters or knob values. The kick engine continues to receive triggers and update internally; the output sample is simply replaced with zero when mute is active.

This has two consequences:

- unmuting does not restore default parameter values;
- if you unmute while an internally generated kick tail is still active, part of that tail may become audible.

If a “hard mute” that also cancels the current envelope is desired, that is a different design and is discussed later.

## 4.3 Why B8 is an enable/bypass switch

B8 does not create a compressor in the conventional threshold-and-ratio sense. It enables a deterministic ducking envelope that is started by the same trigger that starts the kick.

When B8 is on:

```text
trigger → duck envelope jumps to 1 → input gain falls → gain recovers exponentially
```

When B8 is off:

```text
input gain remains 1.0 → stereo audio passes without ducking
```

---

# 5. Performance concept

This patch was designed as an instrument rather than only a laboratory emulation. Every exposed control has a direct performative function.

## 5.1 Tune as register and pressure

`CV_1` moves the body fundamental from approximately C1 to C2. In performance, this can change the kick's perceived scale, weight, and relationship to the room or bass material.

- Lower settings emphasize sub energy and can feel physically larger.
- Higher settings create more audible pitch and can cut through compact systems.
- Small changes can help the kick avoid destructive overlap with another bass voice.

Because the pitch envelope starts above the base frequency and falls toward it, the tune control changes both the tail and the context of the attack sweep.

## 5.2 Decay as density and spatial occupation

`CV_2` controls the body envelope length.

- Short decay leaves more space for rapid patterns and dense percussion.
- Long decay creates sustained sub energy and can blur into bass or drone material.
- At high tempos, long decays overlap, producing continuous low-frequency mass.
- At sparse tempos, long decays create isolated, architectural impacts.

## 5.3 Drive as harmonic translation

A low-frequency sine-like body may be difficult to hear on small speakers even when it is physically strong on a full-range system. Drive produces additional harmonics above the fundamental. These harmonics make the kick more audible on systems that cannot reproduce the deepest frequencies.

At lower settings, drive adds density. At higher settings, it compresses peaks and changes the waveform substantially. Because this patch crossfades between clean and saturated versions, zero drive remains genuinely clean.

## 5.4 Kick level as a live fader

`CV_4` is intentionally simple. It changes only the final kick amplitude. This is useful during:

- breakdowns;
- gradual introductions;
- transitions between external tracks and live modular material;
- balancing against an SP-404, Ableton return, DJ mixer, or other stereo input;
- testing sidechain behavior separately from audible kick level.

## 5.5 B7 mute as structural articulation

The mute is meant for immediate structural changes:

- remove the kick while leaving external audio intact;
- create silent downbeats or breaks;
- prepare a new pitch/decay setting while muted;
- reintroduce the kick without reloading firmware or resetting controls.

## 5.6 B8 sidechain as an arrangement switch

The ducking switch can transform the relationship between the external audio and kick:

- off: kick is layered over the input;
- on: input makes room for each kick;
- fast patterns: rhythmic pumping becomes part of the material;
- long sustained sources: the duck envelope creates repeated articulation;
- already-percussive sources: ducking can create instability or intentional gaps.

---

# 6. The Daisy ecosystem

## 6.1 patch.Init(), Patch SM, and Daisy Seed

Electrosmith's Daisy platform is an embedded audio-development ecosystem. The Patch SM architecture is based on Daisy Seed and adds circuitry intended for Eurorack-level interfacing. The `DaisyPatchSM` C++ class is the board-support layer used by this code.

The class handles hardware-facing tasks such as:

- hardware initialization;
- starting and stopping audio;
- setting sample rate and block size;
- starting ADC conversion;
- reading control values;
- processing digital controls;
- controlling the onboard LED.

Official class reference:

https://electro-smith.github.io/libDaisy/classdaisy_1_1patch__sm_1_1_daisy_patch_s_m.html

## 6.2 libDaisy

**libDaisy** is the hardware-support library. It connects C++ code to Daisy hardware. In this project it provides:

- `DaisyPatchSM`;
- `AudioHandle` buffer types;
- the `Switch` class;
- ADC and control processing;
- audio start-up and callback management;
- sample-rate and block-size configuration.

Official documentation:

https://electro-smith.github.io/libDaisy/

## 6.3 DaisySP

**DaisySP** is Electrosmith's higher-level DSP library. It contains ready-made oscillators, envelopes, filters, reverbs, physical models, and other audio objects.

This project deliberately does **not** include `daisysp.h` and does not use DaisySP's premade kick or drum objects. The point of the patch is to expose and control the underlying math directly.

DaisySP may still exist in the surrounding `DaisyExamples` repository and build environment, but the reference source file only needs:

```cpp
#include "daisy_patch_sm.h"
#include <cmath>
#include <cstdint>
```

This keeps the voice architecture explicit.

## 6.4 DaisyExamples

The official `DaisyExamples` repository provides examples organized by hardware platform and a project/build pipeline involving libDaisy and DaisySP.

Repository:

https://github.com/electro-smith/DaisyExamples

The official project helper can create a Patch SM project with:

```bash
./helper.py create MyProjects/Kick909 --board patch_sm
```

The exact project name and location can be changed.

Official project-creation guide:

https://docs.daisy.audio/tutorials/create-new-project/

## 6.5 The audio callback model

The Daisy audio engine repeatedly calls a function supplied by the programmer:

```cpp
void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
```

The callback receives:

- `in`: arrays containing incoming audio samples;
- `out`: arrays where the code writes outgoing samples;
- `size`: the number of samples in the current audio block.

At a block size of 4 and sample rate of 48,000 Hz:

```text
callback rate = sample rate / block size
              = 48,000 / 4
              = 12,000 callbacks per second
```

Inside every callback, the code:

1. updates hardware controls;
2. checks B7 and B8;
3. reads the four potentiometers and `CV_5`;
4. detects a new trigger;
5. loops through each audio sample;
6. generates one kick sample;
7. optionally ducks the input;
8. adds the kick to both output channels.

## 6.6 Samples, blocks, and continuous sound

Digital audio is not actually continuous inside the processor. It is represented by a sequence of numbers called samples.

At 48 kHz:

```text
one sample lasts 1 / 48,000 second
                 ≈ 20.833 microseconds
```

The code reconstructs the impression of continuous voltage by calculating the next signal value 48,000 times every second.

A block is a small group of samples processed together. Block processing improves efficiency, but a smaller block gives faster control and trigger response. A block size of 4 is deliberately low-latency, although it causes the callback to run very frequently.

## 6.7 Floating-point audio

The code uses `float` values for DSP. A typical internal audio sample is treated as approximately:

```text
-1.0 to +1.0
```

Values outside that range can exist in code, but they may clip later depending on the output path. Gain staging remains important when combining the external input and kick.

---

# 7. Project structure in VS Code

## 7.1 Recommended Windows folder structure

For a Windows 10/11 and VS Code workflow, a practical structure is:

```text
DaisyExamples/
├── libDaisy/
├── DaisySP/
├── helper.py
├── MyProjects/
│   └── Kick909/
│       ├── Makefile
│       └── Kick909.cpp
└── .vscode/
```

The project should reference the shared copies of libDaisy and DaisySP. Do not duplicate the whole libraries inside every project.

## 7.2 Clone the repository with submodules

In Git Bash:

```bash
git clone --recurse-submodules https://github.com/electro-smith/DaisyExamples.git
cd DaisyExamples
```

The `--recurse-submodules` option is important because it retrieves the linked library repositories.

If DaisyExamples was cloned without submodules, run:

```bash
git submodule update --init --recursive
```

## 7.3 Build the libraries

From the DaisyExamples root:

```bash
./ci/build_libs.sh
```

Alternatively, use the VS Code task provided by the Daisy environment, commonly named `build_all`.

## 7.4 Create the project with the helper

From the DaisyExamples root:

```bash
./helper.py create MyProjects/Kick909 --board patch_sm
```

On some systems, use:

```bash
python3 ./helper.py create MyProjects/Kick909 --board patch_sm
```

Open the resulting `Kick909` folder in VS Code.

## 7.5 Minimal Makefile concept

A manually created project typically needs a Makefile similar to:

```make
TARGET = Kick909

CPP_SOURCES = Kick909.cpp

LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
```

The correct number of `../` path levels depends on where the project is stored. If it is in `DaisyExamples/MyProjects/Kick909`, the example above points two directories upward to `DaisyExamples`.

---

# 8. Building and flashing

## 8.1 Command-line build

In the project folder:

```bash
make clean
make
```

A successful build creates firmware files in the `build` folder.

## 8.2 Put the module into DFU mode

Typical Daisy DFU sequence:

1. Hold the BOOT button.
2. Press and release RESET.
3. Release BOOT.

## 8.3 Flash with DFU

```bash
make program-dfu
```

## 8.4 VS Code tasks

Depending on the generated project and installed Electrosmith environment, common tasks include:

- `build`
- `build_all`
- `build_and_program_dfu`

The official guide describes building with VS Code and using Git Bash on Windows:

https://daisy.audio/tutorials/cpp-dev-env/

## 8.5 After flashing

Reconnect or reset the module if necessary. For the initial test:

- turn drive low;
- set level around noon;
- set tune around noon;
- set decay relatively short;
- patch a slow, clean clock into `CV_5`;
- monitor at conservative gain.

---

# 9. TR-909 bass drum circuit overview

The circuit analysis used for this project numbers the major bass-drum sections as follows:

| Circuit section | Function | DSP counterpart in this patch |
|---:|---|---|
| 1 | Control-voltage generator | `base_freq` and ENV-3 frequency equation |
| 2 | VCO | phase accumulator and triangle generator |
| 3 | Diode clipper | `DiodeShape()` |
| 4 | Body VCA Q12 | `body_wave * env1_punch` |
| 5 | ENV-1 | `env1_body` and punch shaping |
| 6 | ENV-3 | `env3_pitch` |
| 7 | Pulse generator | `pulse_env`, `pulse_lpf`, `pulse_bpf` |
| 8 | Noise path | `WhiteNoise()` and `noise_lpf` |
| 9 | Attack VCA Q6 | attack signal multiplied by ENV-2 |
| 10 | ENV-2 | `env2_attack` |
| 11 | Final mix and level | `summed`, drive/output stage, `level` |

## 9.1 Trigger and VCO reset

The source analysis states that every trigger resets the VCO through Q11/C14. The digital counterpart is:

```cpp
phase = 0.0f;
```

This makes every kick begin from a consistent oscillator position. Without phase reset, the oscillator could begin from a different point in its cycle on each trigger, creating different initial amplitudes and less consistent attack behavior.

## 9.2 ENV-3 and pitch movement

ENV-3 creates a fast-decaying pitch-control contour. At the beginning of the kick, the VCO is above the fundamental frequency. As ENV-3 decays, the VCO falls toward the base pitch.

This pitch drop contributes strongly to the perception of impact. A static low oscillator sounds like a bass note. A rapidly descending oscillator sounds more like a struck electronic drum.

## 9.3 Triangle VCO and diode shaping

The circuit analysis describes a triangle VCO followed by a diode clipper that produces a more sine-like waveform.

The code therefore does not directly call `sinf()` for the body. It first creates a triangle:

```cpp
float tri = 1.0f - 4.0f * fabsf(phase - 0.5f);
```

It then bends the triangle through a soft nonlinear function:

```cpp
float body_wave = DiodeShape(tri);
```

This is not a Shockley-equation diode solver, but it represents the functional transition from a linear-sided triangle into a rounded, compressed body waveform.

## 9.4 ENV-1 and punch

The circuit analysis emphasizes that ENV-1 has a compressed drum-like shape and is central to the 909's punch. A plain exponential envelope is therefore reshaped:

```cpp
float env1_punch = powf(env1, 0.65f);
```

Because the exponent is below 1, medium and low envelope values become larger. The body remains stronger during the early decay than it would under a plain exponential.

## 9.5 Pulse and noise attack

The attack is not part of the main oscillator. It is a separate signal made from:

- trigger-derived pulse energy;
- low-pass pulse shaping;
- band-pass pulse shaping;
- filtered noise;
- ENV-2 amplitude control.

This separation makes it possible for the kick to have both a low-frequency body and a short, spectrally broader onset.

---

# 10. Digital signal-flow overview

```text
CV_5 trigger
   |
   +--> ENV-1 body envelope
   +--> ENV-2 attack envelope
   +--> ENV-3 pitch envelope
   +--> pulse helper envelope
   +--> VCO phase reset
   +--> sidechain duck envelope

BODY PATH
CV_1 tune + ENV-3
   -> instantaneous frequency
   -> phase accumulator
   -> triangle waveform
   -> nonlinear diode-like shaping
   -> multiply by punch-shaped ENV-1

ATTACK PATH
pulse envelope
   -> low-pass filter
   -> band-pass filter
   -> weighted pulse mixture

white noise
   -> low-pass filter

pulse + filtered noise
   -> multiply by ENV-2

FINAL KICK
body + attack
   -> drive
   -> output low-pass
   -> DC blocker
   -> CV_4 level

FINAL MODULE OUTPUT
stereo input
   -> optional trigger-derived ducking
   -> add mono kick to left and right
   -> stereo output
```

---

# 11. Complete reference code

The following reference contains the two important corrections discovered during testing:

1. Every envelope trigger begins at `1.0f`, not `10.0f`.
2. B7 initializes `button`, while B8 initializes `toggle`.

```cpp
#include "daisy_patch_sm.h"
#include <cmath>
#include <cstdint>

using namespace daisy;
using namespace patch_sm;

DaisyPatchSM hw;

Switch button; // B7: kick mute button
Switch toggle; // B8: sidechain on/off switch

// ------------------------------------------------------------
// Constants
// ------------------------------------------------------------

static constexpr float TWO_PI = 6.28318530718f;

static constexpr float TRIG_HIGH = 0.25f;
static constexpr float TRIG_LOW  = 0.10f;

// ------------------------------------------------------------
// Utility functions
// ------------------------------------------------------------

static inline float Clamp(float x, float lo, float hi)
{
    if(x < lo)
        return lo;

    if(x > hi)
        return hi;

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
    return out_min * powf(out_max / out_min, x);
}

static inline float OnePoleCoeff(float cutoff_hz, float sample_rate)
{
    cutoff_hz = Clamp(cutoff_hz, 1.0f, sample_rate * 0.45f);
    return 1.0f - expf(-TWO_PI * cutoff_hz / sample_rate);
}

// ------------------------------------------------------------
// Exponential decay envelope
// ------------------------------------------------------------

struct ExpDecayEnv
{
    float sample_rate = 48000.0f;
    float value       = 0.0f;
    float coeff       = 0.999f;

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
// ------------------------------------------------------------

struct OnePoleLPF
{
    float sample_rate = 48000.0f;
    float z           = 0.0f;
    float a           = 0.1f;

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
        float sn    = sinf(omega);
        float cs    = cosf(omega);
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

    float Process(float x)
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
// ------------------------------------------------------------

struct DCBlocker
{
    float x1 = 0.0f;
    float y1 = 0.0f;
    float R  = 0.995f;

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

// ------------------------------------------------------------
// TR-909-inspired bass drum voice
// ------------------------------------------------------------

struct Kick909
{
    float sample_rate = 48000.0f;

    float phase        = 0.0f;
    float base_freq    = 50.0f;
    float drive_amount = 0.0f;
    float level        = 0.8f;

    ExpDecayEnv env1_body;
    ExpDecayEnv env2_attack;
    ExpDecayEnv env3_pitch;
    ExpDecayEnv pulse_env;

    OnePoleLPF   noise_lpf;
    OnePoleLPF   pulse_lpf;
    BiquadBandpass pulse_bpf;
    OnePoleLPF   output_lpf;
    DCBlocker    dc_blocker;

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

    void SetParams(float tune_knob,
                   float decay_knob,
                   float drive_knob,
                   float level_knob)
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
        float bias    = 0.08f * d;

        float saturated = tanhf((x * pregain) + bias) - tanhf(bias);

        float norm = tanhf(pregain);

        if(norm > 0.0001f)
            saturated /= norm;

        float y = (1.0f - d) * x + d * saturated;

        return y;
    }

    float Process()
    {
        float env1 = env1_body.Process();
        float env2 = env2_attack.Process();
        float env3 = env3_pitch.Process();

        float pulse_e = pulse_env.Process();

        float pitch_sweep_octaves = 1.65f;
        float freq = base_freq * powf(2.0f, pitch_sweep_octaves * env3);

        freq = Clamp(freq, 10.0f, sample_rate * 0.40f);

        phase += freq / sample_rate;

        if(phase >= 1.0f)
            phase -= 1.0f;

        float tri = 1.0f - 4.0f * fabsf(phase - 0.5f);
        float body_wave = DiodeShape(tri);

        float env1_punch = powf(env1, 0.65f);
        float body = body_wave * env1_punch;

        float pulse_lp = pulse_lpf.Process(pulse_e);
        float pulse_bp = pulse_bpf.Process(pulse_e);
        float pulse    = (0.62f * pulse_lp) + (0.38f * pulse_bp);

        float raw_noise      = WhiteNoise();
        float filtered_noise = noise_lpf.Process(raw_noise);

        float attack = ((0.90f * pulse) + (0.22f * filtered_noise)) * env2;

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

float duck_env   = 0.0f;
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

bool kick_muted    = false;
bool trig_was_high = false;

// ------------------------------------------------------------
// Audio callback
// ------------------------------------------------------------

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{
    hw.ProcessAllControls();

    button.Debounce();
    toggle.Debounce();

    float tune_knob  = hw.GetAdcValue(CV_1);
    float decay_knob = hw.GetAdcValue(CV_2);
    float drive_knob = hw.GetAdcValue(CV_3);
    float level_knob = hw.GetAdcValue(CV_4);

    kick.SetParams(tune_knob, decay_knob, drive_knob, level_knob);

    if(button.RisingEdge())
    {
        kick_muted = !kick_muted;
    }

    hw.SetLed(!kick_muted);

    bool sidechain_on = toggle.Pressed();

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

    for(size_t i = 0; i < size; i++)
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

    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(4);

    float sample_rate = hw.AudioSampleRate();

    button.Init(DaisyPatchSM::B7, hw.AudioCallbackRate());
    toggle.Init(DaisyPatchSM::B8, hw.AudioCallbackRate());

    kick.Init(sample_rate);

    float duck_release_seconds = 0.135f;
    duck_coeff = expf(-1.0f / (duck_release_seconds * sample_rate));

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
    }
}
```

---

# 12. C++ anatomy for beginners

## 12.1 Preprocessor includes

Lines beginning with `#include` instruct the compiler to make declarations from another file available.

```cpp
#include "daisy_patch_sm.h"
```

The quotation marks indicate a project/library header. This header exposes the Patch SM hardware API.

```cpp
#include <cmath>
#include <cstdint>
```

Angle brackets normally indicate standard-library headers. `cmath` supplies mathematical functions; `cstdint` supplies integer types with known widths.

## 12.2 Namespaces

A namespace groups names to avoid collisions.

```cpp
using namespace daisy;
using namespace patch_sm;
```

Without these lines, many names would need prefixes such as `daisy::` or `patch_sm::`.

## 12.3 Types and variables

A declaration generally follows this shape:

```cpp
type variable_name = initial_value;
```

Example:

```cpp
float base_freq = 50.0f;
```

- `float` is the type.
- `base_freq` is the variable name.
- `50.0f` is the initial value.
- `f` marks the literal as a float.

A Boolean stores true or false:

```cpp
bool kick_muted = false;
```

An unsigned 32-bit integer is used for the random generator:

```cpp
uint32_t rng = 1;
```

## 12.4 Functions

A function has a return type, name, parameters, and body.

```cpp
float Clamp(float x, float lo, float hi)
{
    // function body
}
```

This function:

- returns a `float`;
- is named `Clamp`;
- receives three float parameters;
- executes the code between braces.

A function returning nothing uses `void`:

```cpp
void Trigger()
{
    value = 1.0f;
}
```

## 12.5 Structures

A `struct` groups data and functions into one reusable type.

```cpp
struct ExpDecayEnv
{
    float value;
    void Trigger();
    float Process();
};
```

Later, the code creates several independent envelope objects:

```cpp
ExpDecayEnv env1_body;
ExpDecayEnv env2_attack;
ExpDecayEnv env3_pitch;
```

Each object has its own `value` and `coeff` state.

## 12.6 Member access

A dot accesses something inside an object:

```cpp
env1_body.Trigger();
```

This means: call the `Trigger` function belonging to `env1_body`.

## 12.7 Conditional statements

```cpp
if(kick_muted)
    kick_sample = 0.0f;
```

This executes the assignment only when `kick_muted` is true.

An `else if` supplies another condition:

```cpp
if(new_trigger)
{
    // trigger
}
else if(trigger_returned_low)
{
    // re-arm
}
```

## 12.8 Loops

```cpp
for(size_t i = 0; i < size; i++)
```

This loop begins with `i = 0`, continues while `i < size`, and adds one after every pass.

It processes every sample in the audio block.

## 12.9 Operators used often

| Operator | Meaning |
|---|---|
| `=` | assign a value |
| `+` | addition |
| `-` | subtraction |
| `*` | multiplication |
| `/` | division |
| `+=` | add and store |
| `*=` | multiply and store |
| `!` | logical NOT |
| `&&` | logical AND |
| `<`, `>` | comparisons |
| `.` | access object member |

Example:

```cpp
phase += freq / sample_rate;
```

is shorthand for:

```cpp
phase = phase + (freq / sample_rate);
```

---

# 13. Detailed code walkthrough

## 13.1 Hardware objects

```cpp
DaisyPatchSM hw;
Switch button;
Switch toggle;
```

`hw` represents the board. `button` and `toggle` are digital-control helper objects. They must be initialized with the correct hardware pins in `main()`.

A previously discovered bug initialized `button` twice:

```cpp
button.Init(DaisyPatchSM::B7, ...);
button.Init(DaisyPatchSM::B8, ...); // incorrect
```

This reassigned the `button` object to B8 and left `toggle` uninitialized. The corrected code is:

```cpp
button.Init(DaisyPatchSM::B7, hw.AudioCallbackRate());
toggle.Init(DaisyPatchSM::B8, hw.AudioCallbackRate());
```

## 13.2 Constants and trigger thresholds

```cpp
static constexpr float TRIG_HIGH = 0.25f;
static constexpr float TRIG_LOW  = 0.10f;
```

A trigger starts only when the input rises above `TRIG_HIGH`. The detector does not re-arm until the input falls below `TRIG_LOW`.

Two thresholds provide hysteresis. Hysteresis prevents a noisy or slowly moving voltage near one threshold from producing repeated triggers.

## 13.3 `Clamp`

```cpp
static inline float Clamp(float x, float lo, float hi)
```

This prevents parameters from leaving useful or safe ranges. For example, the filter cutoff cannot become negative, and a knob value should remain between 0 and 1.

## 13.4 Linear and exponential mapping

A normalized ADC reading is typically treated as 0 to 1. That range must be converted into musical units.

Linear mapping:

```cpp
out_min + x * (out_max - out_min)
```

Exponential mapping:

```cpp
out_min * powf(out_max / out_min, x)
```

Exponential mapping is used for frequency and decay because ratio-based spacing usually feels more even across the knob.

## 13.5 Envelope object

`ExpDecayEnv` stores:

- sample rate;
- current envelope value;
- per-sample decay coefficient.

Triggering sets the envelope to 1:

```cpp
value = 1.0f;
```

Processing multiplies it by a number slightly below 1:

```cpp
value *= coeff;
```

A previously tested faulty value was:

```cpp
value = 10.0f;
```

That made ENV-3 enormous. Since ENV-3 appears in an exponent in the pitch formula, it forced the frequency to the safety clamp near 19.2 kHz at every kick. This produced the high-pitched artifact.

## 13.6 One-pole low-pass filter

The internal state `z` is repeatedly moved toward the input:

```cpp
z += a * (x - z);
```

This is equivalent to:

```text
new output = old output + fraction of the difference
```

Rapid changes are smoothed more strongly than slow changes, which is the defining behavior of a low-pass filter.

## 13.7 Biquad band-pass filter

The band-pass is a second-order filter. It remembers two previous inputs and two previous outputs. The equation is:

```text
y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]
       - a1*y[n-1] - a2*y[n-2]
```

The coefficients are calculated from center frequency, sample rate, and Q.

This filter is used only for the pulse path, reflecting the 909 analysis that the trigger pulse is shaped by both low-pass and band-pass networks.

## 13.8 DC blocker

The drive function adds slight asymmetry through bias. Asymmetry can move the waveform's average away from zero. The DC blocker uses:

```text
y[n] = x[n] - x[n-1] + R*y[n-1]
```

A constant input disappears because `x[n] - x[n-1]` becomes zero. Very low audio frequencies are retained because `R` is close to 1.

## 13.9 Kick state

The `Kick909` structure stores all persistent state required by the voice:

- oscillator phase;
- base frequency;
- drive amount;
- output level;
- four envelope objects;
- four filter/conditioning objects;
- random-number state.

These values must persist across callback calls. If phase or filter memory were recreated from zero on every sample, the oscillator and filters would not work.

## 13.10 `Init`

`Init` receives the actual sample rate and configures all time-dependent objects. Every envelope and filter needs sample rate because its per-sample behavior changes when the number of samples per second changes.

## 13.11 `SetParams`

This function receives normalized control values once per block.

Pitch:

```cpp
base_freq = MapExp(tune_knob, 32.70f, 65.41f);
```

Body decay:

```cpp
float decay_seconds = MapExp(decay_knob, 0.10f, 1.60f);
```

Drive:

```cpp
drive_amount = Clamp(drive_knob, 0.0f, 1.0f);
```

Kick-only level:

```cpp
level = level_knob * 1.25f;
```

The fixed pitch, transient, and pulse decay values remain internal design parameters.

## 13.12 `Trigger`

Every new trigger:

- restarts ENV-1;
- restarts ENV-2;
- restarts ENV-3;
- restarts the pulse helper;
- resets oscillator phase.

No knob settings are reset.

## 13.13 White-noise generator

The noise generator is a linear congruential pseudo-random generator:

```cpp
rng = 1664525UL * rng + 1013904223UL;
```

The integer bits are converted into a bipolar float between approximately -1 and +1. This is computationally inexpensive and suitable for the short filtered attack-noise role.

## 13.14 Diode-like body shaping

```cpp
float y = tanhf(x * shape_gain);
```

`tanh` is nearly linear around zero but flattens toward ±1 at larger magnitudes. Passing the triangle through this curve rounds and compresses it.

This is a functional approximation. It does not model individual diode voltage, temperature, or current equations.

## 13.15 Drive

Drive contains four conceptual parts:

1. **Pre-gain**

   ```cpp
   float pregain = 1.0f + 24.0f * d * d;
   ```

2. **Bias/asymmetry**

   ```cpp
   float bias = 0.08f * d;
   ```

3. **Soft saturation**

   ```cpp
   tanhf((x * pregain) + bias)
   ```

4. **Clean/saturated blend**

   ```cpp
   (1.0f - d) * x + d * saturated
   ```

At zero drive, the clean path is fully selected. At full drive, the saturated path is fully selected.

## 13.16 Per-sample kick generation

The `Process()` function executes the following sequence every sample:

1. update envelopes;
2. calculate current frequency;
3. advance oscillator phase;
4. generate triangle;
5. shape triangle;
6. shape ENV-1 for punch;
7. calculate body;
8. calculate filtered pulse;
9. calculate filtered noise;
10. calculate attack;
11. sum body and attack;
12. apply drive;
13. smooth output;
14. remove DC;
15. apply level.

## 13.17 Sidechain envelope

`duck_env` begins at 1 after every trigger and decays exponentially. It is converted into input gain with:

```cpp
input_gain = 1.0f - duck_depth * duck;
```

At the default `duck_depth = 0.75`:

- maximum duck gives gain 0.25;
- recovered duck gives gain 1.0.

## 13.18 Mute logic

```cpp
if(button.RisingEdge())
    kick_muted = !kick_muted;
```

`RisingEdge()` returns true only when the button changes from unpressed to pressed. The `!` operator flips the Boolean.

Later:

```cpp
if(kick_muted)
    kick_sample = 0.0f;
```

The engine continues to run. Only the outgoing kick sample is silenced.

## 13.19 Switch logic

```cpp
bool sidechain_on = toggle.Pressed();
```

If the physical orientation is opposite to the desired labeling, invert the logic:

```cpp
bool sidechain_on = !toggle.Pressed();
```

Do not invert it merely because an earlier uninitialized-switch bug caused odd behavior. First verify correct initialization.

## 13.20 Trigger edge detector

```cpp
if(!trig_was_high && trig_cv > TRIG_HIGH)
```

This requires the previous state to be low and the current voltage to be high. A held-high gate therefore creates only one kick.

The detector re-arms here:

```cpp
else if(trig_was_high && trig_cv < TRIG_LOW)
```

## 13.21 Input pass-through and final mix

The stereo input is read independently:

```cpp
float dry_l = in[0][i];
float dry_r = in[1][i];
```

After optional ducking, the mono kick is added to both channels:

```cpp
out[0][i] = dry_l + kick_sample;
out[1][i] = dry_r + kick_sample;
```

This simple sum can exceed nominal digital headroom. Gain staging is discussed later.

---

# 14. Mathematics and DSP foundations

## 14.1 Normalized controls

ADC controls are treated as normalized values:

```text
0.0 = minimum
1.0 = maximum
```

A normalized value is dimensionless. It becomes meaningful only after mapping it to frequency, time, gain, or another parameter.

## 14.2 Exponential envelope

The continuous-time envelope is:

```text
V(t) = V0 * e^(-t / tau)
```

Where:

- `V0` is the initial value;
- `t` is elapsed time;
- `tau` is the time constant;
- `e` is the base of the natural logarithm.

The code uses a per-sample multiplier:

```text
coefficient = e^(-1 / (tau * sample_rate))
value[n] = value[n-1] * coefficient
```

After `tau` seconds, an ideal exponential has fallen to about 36.8% of its initial value. Therefore, the number passed as `decay_seconds` is a time constant, not necessarily the exact time until absolute silence.

## 14.3 Why `value = 10` produced a high tone

The instantaneous pitch equation is:

```text
frequency = base_frequency * 2^(sweep_octaves * ENV3)
```

With the correct trigger value:

```text
ENV3 = 1
sweep = 1.65 octaves
multiplier = 2^1.65 ≈ 3.14
```

A 50 Hz base begins near 157 Hz.

With the faulty value:

```text
ENV3 = 10
multiplier = 2^(16.5) ≈ 92,682
50 Hz * 92,682 ≈ 4.63 MHz
```

The code clamps that impossible value to:

```text
0.40 * 48,000 = 19,200 Hz
```

That is why every kick produced a very high-frequency spike.

## 14.4 Frequency in octaves

An octave is a doubling of frequency:

```text
frequency_after_octaves = base * 2^octaves
```

Examples:

```text
base * 2^0 = base
base * 2^1 = 2 * base
base * 2^2 = 4 * base
base * 2^-1 = base / 2
```

Using octaves keeps pitch motion perceptually consistent.

## 14.5 Phase accumulator

The oscillator phase update is:

```text
phase_increment = frequency / sample_rate
phase = phase + phase_increment
```

At 48 Hz and 48 kHz:

```text
increment = 48 / 48,000 = 0.001
```

One complete cycle requires 1,000 samples, which equals 1/48 second.

## 14.6 Triangle-wave equation

```cpp
float tri = 1.0f - 4.0f * fabsf(phase - 0.5f);
```

At several phase points:

| Phase | Calculation | Triangle value |
|---:|---|---:|
| 0.00 | `1 - 4*0.5` | -1 |
| 0.25 | `1 - 4*0.25` | 0 |
| 0.50 | `1 - 4*0` | +1 |
| 0.75 | `1 - 4*0.25` | 0 |
| 1.00 | `1 - 4*0.5` | -1 |

This makes a linear rise from -1 to +1 and a linear fall back to -1.

## 14.7 Hyperbolic tangent

The nonlinear function is:

```text
tanh(x) = (e^x - e^-x) / (e^x + e^-x)
```

Near zero:

```text
tanh(x) ≈ x
```

For large positive input:

```text
tanh(x) approaches +1
```

For large negative input:

```text
tanh(x) approaches -1
```

This produces smooth rather than abrupt clipping.

## 14.8 Punch-envelope power curve

```cpp
powf(env1, 0.65f)
```

For values between 0 and 1, an exponent below 1 makes the result larger:

| Raw ENV-1 | `ENV-1^0.65` |
|---:|---:|
| 1.00 | 1.00 |
| 0.75 | about 0.83 |
| 0.50 | about 0.64 |
| 0.25 | about 0.41 |
| 0.10 | about 0.22 |

This creates a stronger early and middle contour.

## 14.9 One-pole low-pass coefficient

```text
a = 1 - e^(-2*pi*fc/fs)
```

Where:

- `fc` is cutoff frequency;
- `fs` is sample rate.

Filter update:

```text
z[n] = z[n-1] + a * (x[n] - z[n-1])
```

This is a discrete approximation of a first-order analog low-pass response.

## 14.10 Biquad band-pass and Q

Q describes the relationship between center frequency and bandwidth:

```text
Q = center_frequency / bandwidth
```

Higher Q generally means a narrower, more resonant pass band. Lower Q means a wider band.

The chosen Q of 0.85 is relatively broad. It shapes the pulse without creating an extreme ringing tone.

## 14.11 VCA as multiplication

A voltage-controlled amplifier changes audio amplitude according to a control voltage. In DSP, its simplest equivalent is multiplication:

```text
output = audio * envelope
```

Body VCA:

```cpp
body = body_wave * env1_punch;
```

Attack VCA:

```cpp
attack = attack_source * env2;
```

Input ducking VCA:

```cpp
dry_l *= input_gain;
dry_r *= input_gain;
```

## 14.12 Decibels and duck depth

Amplitude gain converts to decibels with:

```text
dB = 20 * log10(gain)
```

At the default maximum duck gain of 0.25:

```text
20 * log10(0.25) ≈ -12.04 dB
```

So `duck_depth = 0.75` creates approximately 12 dB maximum attenuation.

## 14.13 Digital summing and headroom

The output is:

```text
output = ducked_input + kick
```

If both signals approach full scale simultaneously, the sum may exceed ±1. One possible protective change is:

```cpp
out[0][i] = 0.7f * (dry_l + kick_sample);
out[1][i] = 0.7f * (dry_r + kick_sample);
```

This reduces overall level but gives more headroom.

Another option is to reduce only the maximum kick level:

```cpp
level = level_knob * 0.85f;
```

---

# 15. Trigger, button, switch, mute, and sidechain logic

## 15.1 Why trigger detection needs memory

A clock pulse may remain high across many samples and callbacks. If the code triggered whenever the value was high, one physical pulse could produce thousands of retriggers.

The `trig_was_high` variable remembers the previous state.

New trigger:

```text
previous state = low
current voltage > high threshold
```

Re-arm:

```text
previous state = high
current voltage < low threshold
```

## 15.2 Why buttons need debouncing

A mechanical button does not produce one ideal transition. Its contacts can physically bounce, creating several fast on/off transitions. `Debounce()` filters this behavior so one press produces one logical event.

## 15.3 Latched mute versus momentary mute

Current design:

```text
press once -> mute remains on
press again -> mute turns off
```

Momentary alternative:

```cpp
bool kick_muted = button.Pressed();
```

This would mute only while the button is held. The latched version is more useful for structural performance changes.

## 15.4 Soft mute versus hard mute

Current soft mute:

```cpp
if(kick_muted)
    kick_sample = 0.0f;
```

The internal envelopes continue.

A hard mute could additionally cancel envelopes:

```cpp
void Kill()
{
    env1_body.value = 0.0f;
    env2_attack.value = 0.0f;
    env3_pitch.value = 0.0f;
    pulse_env.value = 0.0f;
}
```

However, this exposes internal members and changes performance behavior. It should be added deliberately rather than assumed.

## 15.5 Sidechain follows trigger, not audible kick level

`TriggerDuck()` is called whenever a trigger is detected, even if:

- the kick is muted;
- kick level is zero;
- drive is zero.

This can be useful: the module can function as a trigger-driven ducking processor even with no audible kick. If ducking should occur only when the kick is audible, change the trigger logic to check mute and level.

Example:

```cpp
kick.Trigger();

if(!kick_muted && level_knob > 0.001f)
    TriggerDuck();
```

---

# 16. How to modify the kick

Make one change at a time, rebuild, flash, and compare. Keep a known-good copy in version control.

## 16.1 Pitch range

Current:

```cpp
base_freq = MapExp(tune_knob, 32.70f, 65.41f);
```

Deeper:

```cpp
base_freq = MapExp(tune_knob, 25.0f, 55.0f);
```

Higher:

```cpp
base_freq = MapExp(tune_knob, 40.0f, 90.0f);
```

Do not extend the range blindly. Very low frequencies may require long decay and sufficient playback bandwidth to be perceptible.

## 16.2 Pitch-sweep depth

Current:

```cpp
float pitch_sweep_octaves = 1.65f;
```

Subtle:

```cpp
float pitch_sweep_octaves = 0.9f;
```

Aggressive:

```cpp
float pitch_sweep_octaves = 2.2f;
```

Excessive depth can make the attack sound like a laser or high-pitched chirp.

## 16.3 Pitch-envelope speed

Current:

```cpp
env3_pitch.SetDecay(0.035f);
```

Faster:

```cpp
env3_pitch.SetDecay(0.020f);
```

Slower:

```cpp
env3_pitch.SetDecay(0.060f);
```

A slower sweep becomes more tonally obvious and can move toward tom-like behavior.

## 16.4 Body-decay range

Current:

```cpp
float decay_seconds = MapExp(decay_knob, 0.10f, 1.60f);
```

Tighter:

```cpp
float decay_seconds = MapExp(decay_knob, 0.05f, 0.80f);
```

Longer:

```cpp
float decay_seconds = MapExp(decay_knob, 0.10f, 3.00f);
```

Remember that this number is an exponential time constant rather than an exact silence time.

## 16.5 ENV-1 punch shape

Current:

```cpp
float env1_punch = powf(env1, 0.65f);
```

Closer to plain exponential:

```cpp
float env1_punch = powf(env1, 0.9f);
```

More compressed/held:

```cpp
float env1_punch = powf(env1, 0.5f);
```

Very small exponents can make the tail remain too strong and may reduce dynamic articulation.

## 16.6 Diode-like shape

Current:

```cpp
float shape_gain = 1.85f;
```

Less shaped:

```cpp
float shape_gain = 0.9f;
```

More saturated:

```cpp
float shape_gain = 3.0f;
```

This is internal body-wave shaping and is separate from the user-controlled drive stage.

## 16.7 Pulse low-pass cutoff

Current:

```cpp
pulse_lpf.Init(sample_rate, 8500.0f);
```

Darker pulse:

```cpp
pulse_lpf.Init(sample_rate, 5000.0f);
```

Brighter pulse:

```cpp
pulse_lpf.Init(sample_rate, 12000.0f);
```

## 16.8 Pulse band-pass center and Q

Current:

```cpp
pulse_bpf.Init(sample_rate, 4800.0f, 0.85f);
```

Lower, more knock-like:

```cpp
pulse_bpf.Init(sample_rate, 2500.0f, 0.85f);
```

Higher, more click-like:

```cpp
pulse_bpf.Init(sample_rate, 7000.0f, 1.0f);
```

More resonant:

```cpp
pulse_bpf.Init(sample_rate, 4800.0f, 2.0f);
```

High Q can produce audible ringing and a pitched click.

## 16.9 Pulse/noise balance

Current pulse mixture:

```cpp
float pulse = (0.62f * pulse_lp) + (0.38f * pulse_bp);
```

Current attack mixture:

```cpp
float attack = ((0.90f * pulse) + (0.22f * filtered_noise)) * env2;
```

More noise:

```cpp
float attack = ((0.80f * pulse) + (0.40f * filtered_noise)) * env2;
```

More defined pulse:

```cpp
float attack = ((1.10f * pulse) + (0.12f * filtered_noise)) * env2;
```

## 16.10 Attack duration

Current:

```cpp
env2_attack.SetDecay(0.014f);
```

Shorter:

```cpp
env2_attack.SetDecay(0.008f);
```

Longer:

```cpp
env2_attack.SetDecay(0.030f);
```

Long attack decay can make the noise more audible and less like a compact click.

## 16.11 Drive intensity

Current maximum pre-gain:

```cpp
float pregain = 1.0f + 24.0f * d * d;
```

Gentler:

```cpp
float pregain = 1.0f + 10.0f * d * d;
```

More aggressive:

```cpp
float pregain = 1.0f + 40.0f * d * d;
```

## 16.12 Drive asymmetry

Current:

```cpp
float bias = 0.08f * d;
```

Symmetric:

```cpp
float bias = 0.0f;
```

More asymmetric:

```cpp
float bias = 0.15f * d;
```

More bias can increase even-harmonic content but may also increase DC-offset stress before the blocker.

## 16.13 Kick-level range

Current:

```cpp
level = level_knob * 1.25f;
```

Safer headroom:

```cpp
level = level_knob * 0.85f;
```

Higher output should be used cautiously because the kick is later added to the input.

## 16.14 Sidechain depth

Current:

```cpp
float duck_depth = 0.75f;
```

Subtle:

```cpp
float duck_depth = 0.35f;
```

Deep:

```cpp
float duck_depth = 0.95f;
```

At 0.95, maximum input gain is 0.05, approximately -26 dB.

## 16.15 Sidechain release

Current:

```cpp
float duck_release_seconds = 0.135f;
```

Tight:

```cpp
float duck_release_seconds = 0.070f;
```

Pumpier:

```cpp
float duck_release_seconds = 0.300f;
```

At high tempos, long release times may prevent the input from fully recovering between triggers.

## 16.16 Smooth duck attack

The current duck attack is instantaneous. This can sharply cut an input transient. A smoothed attack would require a second coefficient or a one-pole smoother rather than setting `duck_env` directly to 1.

Concept:

```cpp
duck_target = 1.0f;
duck_env += attack_coeff * (duck_target - duck_env);
```

This could allow some of the external transient through, similar to the smoothing ideas available in Bohm:Performer.

## 16.17 Frequency-selective ducking

The current sidechain attenuates the entire stereo input. A future revision could split the input into low and high bands, duck only the lows, then recombine them. This would preserve brightness while making low-frequency space for the kick.

---

# 17. Patching and performance use

## 17.1 Basic standalone kick

```text
clock/gate source -> CV_5
Patch.Init audio out -> mixer/interface
```

No audio input is required. The input channels will simply contribute silence.

## 17.2 With Intellijel µMIDI or another MIDI-to-CV converter

```text
Ableton/MIDI clock or note pattern
   -> MIDI-to-CV gate/trigger output
   -> CV_5
```

Use a clean trigger or gate. The hysteresis detector accepts a held gate as one event and waits for it to return low.

## 17.3 With Pamela's NEW Workout

```text
Pam output -> CV_5
```

Use the Pam output level and pulse width that reliably cross the trigger threshold. Pulse width can be short because the firmware detects the rising edge rather than using gate duration for sustain.

## 17.4 With Sir CLK or a modular master clock

A master clock can synchronize the kick with MIDI devices and modular sources. The Patch.Init requires a trigger pattern, not necessarily the undivided master clock. Use a divider, sequencer, logic module, or MIDI/CV pattern source when the kick should not occur on every clock pulse.

## 17.5 Processing SP-404 or another stereo device

```text
SP-404 stereo output
   -> Patch.Init stereo input
Patch.Init stereo output
   -> mixer/audio interface
```

B8 can then enable trigger-derived ducking of the sampler while the kick is mixed in. Watch gain staging: line-level and Eurorack-level systems may require appropriate interface modules or attenuation.

## 17.6 With Ableton and modular hardware

A hybrid routing example:

```text
Ableton rhythmic/MIDI source
   -> MIDI-to-CV trigger
   -> CV_5

Ableton or external stereo bus
   -> Patch.Init audio input

Patch.Init output
   -> audio interface return
```

The module can function simultaneously as kick voice and sidechain-performance processor.

## 17.7 With TouchDesigner-driven performance

TouchDesigner can influence the system indirectly through MIDI, OSC-to-MIDI, CV hardware, or an audio/CV interface. Useful mappings include:

- generative triggers to `CV_5`;
- external modulation into tune or decay controls if the hardware permits combined pot/CV behavior;
- visual events synchronized to the same trigger used by the kick;
- using B7 and B8 as immediate physical overrides during computer-driven systems.

## 17.8 Two-rack workflow

In a split Eurorack system, the kick can remain in the rhythm/control rack while audio from a sampler, noise source, or second rack passes through it. Because the patch provides both generation and ducking, it can reduce the need for a separate sidechain VCA in compact performance configurations.

## 17.9 Live transition techniques

- **Silent preparation:** mute with B7, change pitch/decay/drive, then unmute.
- **Ghost pumping:** turn kick level to zero but leave B8 on; triggers still duck the input.
- **Layered impact:** B8 off, raise level and drive, let kick stack directly over source.
- **Space-making mode:** B8 on, moderate kick level, deep ducking.
- **Sub removal:** raise tune before moving into material with a strong bass line.
- **Long-tail transition:** lengthen decay and slowly lower kick level rather than muting abruptly.

---

# 18. Testing and calibration procedure

## 18.1 Stage 1: pass-through only

1. Connect a known stereo source to the audio input.
2. Do not patch `CV_5` yet.
3. Confirm the source passes to both outputs.
4. Cycle B8 and confirm that nothing should be ducked without triggers.

## 18.2 Stage 2: trigger detection

1. Disconnect or lower the external audio.
2. Set kick level to noon.
3. Patch a slow trigger into `CV_5`.
4. Confirm exactly one kick per pulse.
5. If no kick occurs, lower `TRIG_HIGH` cautiously.
6. If multiple kicks occur per pulse, increase hysteresis or inspect the trigger source.

## 18.3 Stage 3: control sweep

Test one control at a time:

- tune from minimum to maximum;
- decay from minimum to maximum;
- drive from zero to maximum;
- kick level from zero to maximum.

Listen for discontinuities, digital clicks, unexpected silence, or excessive clipping.

## 18.4 Stage 4: B7

1. Press B7 once.
2. LED should indicate the mute state according to firmware logic.
3. Kick should mute while input still passes.
4. Press again.
5. Kick should return without parameter reset.

## 18.5 Stage 5: B8

1. Pass sustained audio through the module.
2. Trigger the kick slowly.
3. Put B8 in one position and listen.
4. Move B8 to the other position.
5. One position should produce ducking.
6. If labeling is inverted, invert `toggle.Pressed()` in code.

## 18.6 Stage 6: gain and headroom

1. Use a strong external input and strong kick simultaneously.
2. Listen for hard clipping.
3. Lower `level` scaling if required.
4. Consider a master attenuation factor before writing to `out`.

---

# 19. Troubleshooting

## 19.1 Very high-pitched sound on every kick

Check:

```cpp
void Trigger()
{
    value = 1.0f;
}
```

Do not use `10.0f`. A value of 10 makes the pitch exponent enormous and forces the frequency clamp near 19.2 kHz.

Also inspect:

```cpp
float pitch_sweep_octaves = 1.65f;
env3_pitch.SetDecay(0.035f);
```

If the trigger level is correct but the onset is still too high, reduce sweep depth or shorten pitch-envelope time.

## 19.2 B7 does nothing

Verify initialization:

```cpp
button.Init(DaisyPatchSM::B7, hw.AudioCallbackRate());
```

Verify callback processing:

```cpp
button.Debounce();
```

Verify edge logic:

```cpp
if(button.RisingEdge())
    kick_muted = !kick_muted;
```

## 19.3 B8 mutes the kick or behaves unpredictably

Verify that B8 initializes `toggle`, not `button`:

```cpp
toggle.Init(DaisyPatchSM::B8, hw.AudioCallbackRate());
```

An uninitialized `toggle` can report meaningless state. Initializing `button` twice causes B8 to take over the mute object.

## 19.4 B8 works backward

After confirming correct initialization, invert:

```cpp
bool sidechain_on = !toggle.Pressed();
```

## 19.5 No trigger from CV_5

Possible causes:

- source voltage does not cross `TRIG_HIGH`;
- wrong jack or ADC identifier;
- ADC was not started;
- trigger never returns below `TRIG_LOW`;
- source and module grounds are not correctly referenced.

Try:

```cpp
static constexpr float TRIG_HIGH = 0.15f;
static constexpr float TRIG_LOW  = 0.05f;
```

Do not lower thresholds excessively without checking noise and false triggering.

## 19.6 Double triggering

Increase separation between thresholds:

```cpp
TRIG_HIGH = 0.30f;
TRIG_LOW  = 0.08f;
```

Also shorten or clean the trigger source.

## 19.7 Kick is silent but pass-through works

Check:

- B7 mute state;
- `CV_4` level;
- trigger detection;
- `kick.Process()` is still called;
- `level` is nonzero;
- kick is added to outputs.

## 19.8 Audio input disappears

Check:

- sidechain switch state;
- `duck_depth` is not above 1;
- `ProcessDuck()` returns toward zero;
- `duck_coeff` was calculated from a valid sample rate;
- input routing and physical jack configuration.

## 19.9 Harsh clipping

Reduce:

```cpp
level = level_knob * 0.85f;
```

Or add master attenuation:

```cpp
out[0][i] = 0.7f * (dry_l + kick_sample);
out[1][i] = 0.7f * (dry_r + kick_sample);
```

## 19.10 Build cannot find `daisy_patch_sm.h`

Check `LIBDAISY_DIR` in the Makefile and confirm libDaisy submodules were cloned and built.

## 19.11 `make program-dfu` cannot find device

Confirm:

- USB cable supports data;
- module is in DFU mode;
- Windows driver is correct;
- no other application is holding the device;
- toolchain and `dfu-util` installation are complete.

Official troubleshooting and setup resources are listed in the references.

---

# 20. Possible future revisions

## 20.1 Component-derived constants

Replace hand-tuned filter frequencies and envelope times with values calculated from resistor and capacitor values:

```text
tau = R * C
fc = 1 / (2*pi*R*C)
```

This would make the model more explicitly tied to the service schematic.

## 20.2 More accurate diode network

Replace `tanh` body shaping with an iterative antiparallel-diode solver using the Shockley diode equation. This would increase component realism and CPU cost.

## 20.3 More accurate ENV-1 contour

The current power curve is a simple way to create punch. A multi-stage envelope or fitted curve based on measured 909 envelope voltage could be more accurate.

## 20.4 Oversampled drive

Nonlinear processing creates harmonics that can alias. Oversampling the drive stage could reduce digital aliasing, at additional CPU cost.

## 20.5 Accent input

The original 909 accent affects envelope/VCA behavior. A future control mode could use an available CV input to scale body and attack intensity rather than only final output.

## 20.6 Trigger velocity

Trigger height could be measured and mapped to velocity. This would require reliable knowledge of Patch.Init ADC scaling and the trigger source voltage range.

## 20.7 Sidechain smoothing

Add adjustable duck attack and release, possibly by repurposing a button-modified pot mode.

## 20.8 Frequency-selective ducking

Split the external input and duck only low frequencies. This would better preserve vocals, cymbals, and upper-frequency texture.

## 20.9 Control smoothing

The current knobs update every block. Fast ADC movement or noisy control signals could produce zippering. One-pole smoothing for tune, decay, drive, and level would improve stability.

## 20.10 Parameter modes

B7 or B8 could be held while turning a pot to access secondary parameters such as:

- pitch-sweep depth;
- pitch-sweep time;
- pulse/noise balance;
- sidechain depth;
- sidechain release;
- attack tone.

This would expand the instrument without adding hardware controls, but would increase interface complexity.

---

# 21. Glossary

**ADC**  
Analog-to-digital converter. Converts a physical voltage into a number the processor can read.

**Aliasing**  
Digital artifacts created when generated frequencies exceed what the sample rate can represent and fold into lower frequencies.

**Amplitude**  
Signal magnitude. Often perceived as loudness, though perceived loudness also depends on frequency and duration.

**Band-pass filter**  
A filter that passes a middle frequency band while reducing frequencies below and above it.

**Biquad**  
A second-order digital filter using two previous input samples and two previous output samples.

**Block size**  
Number of audio samples processed per callback invocation.

**Boolean**  
A value that is either true or false.

**Callback**  
A function that the audio system repeatedly calls to process audio.

**Clamp**  
Limit a number so it cannot go below a minimum or above a maximum.

**Coefficient**  
A stored number that controls a mathematical relationship, such as filter or envelope rate.

**CV**  
Control voltage. A voltage used to control a parameter or trigger an event.

**DC offset**  
A nonzero average value that shifts a waveform away from zero.

**Decay**  
The falling portion of an envelope after its initial trigger.

**DFU**  
Device Firmware Upgrade mode, used to flash firmware over USB.

**Drive**  
Gain applied before nonlinear saturation, often followed by output compensation or mixing.

**DSP**  
Digital signal processing: mathematical manipulation of sampled signals.

**Envelope**  
A time-varying control shape used to change amplitude, pitch, filter cutoff, or another parameter.

**Exponential**  
A curve involving repeated multiplication. Common in analog capacitor charge/discharge and musical pitch relationships.

**Float**  
A floating-point numerical type used for fractional values and most Daisy audio DSP.

**Fundamental frequency**  
The primary or lowest periodic frequency perceived as the pitch of a tone.

**Gain**  
A multiplier applied to signal amplitude.

**Gate**  
A control signal that remains high for a duration. This patch treats a gate's rising edge as a trigger.

**Headroom**  
Available amplitude above the normal operating level before clipping.

**Hysteresis**  
Using separate high and low thresholds to prevent unstable switching near one threshold.

**Low-pass filter**  
A filter that passes low frequencies and reduces high frequencies.

**Mono**  
One audio channel. The generated kick is mono.

**Namespace**  
A C++ name-grouping mechanism.

**Normalized value**  
A value placed into a standard range, commonly 0 to 1.

**Oscillator**  
A system that generates a repeating waveform.

**Phase**  
Current position within one oscillator cycle.

**Pseudo-random**  
A deterministic numerical sequence designed to appear random.

**Q**  
A parameter describing filter bandwidth/resonance around a center frequency.

**Sample**  
One numerical measurement of an audio signal at one instant.

**Sample rate**  
Number of samples processed per second.

**Saturation**  
Nonlinear compression and reshaping of a signal as it approaches a limit.

**Sidechain ducking**  
Reducing one signal according to an event or control derived from another signal. Here, the trigger reduces external input gain.

**Soft clipping**  
Gradual nonlinear limiting rather than abrupt flat clipping.

**Stereo**  
Two audio channels, left and right.

**Struct**  
A C++ type that groups data and functions.

**Time constant**  
A parameter controlling exponential response speed. After one time constant, a decay reaches approximately 36.8% of its initial value.

**Trigger**  
A short event signal used to start an envelope or sound.

**VCA**  
Voltage-controlled amplifier. In DSP, commonly represented as multiplication of audio by a control envelope.

**VCO**  
Voltage-controlled oscillator. In DSP, represented by an oscillator whose frequency is changed by a control value.

---

# 22. References

## Electrosmith / Daisy

- Daisy C++ development environment:  
  https://daisy.audio/tutorials/cpp-dev-env/

- Create a new Daisy C++ project:  
  https://docs.daisy.audio/tutorials/create-new-project/

- libDaisy documentation:  
  https://electro-smith.github.io/libDaisy/

- `DaisyPatchSM` class reference:  
  https://electro-smith.github.io/libDaisy/classdaisy_1_1patch__sm_1_1_daisy_patch_s_m.html

- `Switch` class reference:  
  https://electro-smith.github.io/libDaisy/classdaisy_1_1_switch.html

- Daisy audio introduction:  
  https://electro-smith.github.io/libDaisy/md_doc_2md_2__a3___getting-_started-_audio.html

- ADC tutorial:  
  https://daisy.audio/tutorials/_a4_Getting-Started-ADCs/

- DaisyExamples repository:  
  https://github.com/electro-smith/DaisyExamples

- Patch SM HardwareTest example, including B7/B8 switch initialization:  
  https://github.com/electro-smith/DaisyExamples/blob/master/patch_sm/HardwareTest/HardwareTest.cpp

## Ohmforce Bohm

- Bohm product page:  
  https://ohmforce.com/products/bohm

- Bohm manual:  
  https://bohm-eurorack-manual.readthedocs.io/en/latest/

- Bohm functions and routing:  
  https://bohm-eurorack-manual.readthedocs.io/en/latest/functions/index.html

- Bohm control overview:  
  https://bohm-eurorack-manual.readthedocs.io/en/latest/overview/index.html

- Bohm core models:  
  https://bohm-eurorack-manual.readthedocs.io/en/latest/library/index.html

## Roland TR-909 and circuit analysis

- Roland TR-909 service manual copy used during circuit study:  
  https://www.polynominal.com/site/studio/gear/drum/roland-tr909/roland-tr909-service-manual.pdf

- Network-909 bass drum circuit analysis:  
  http://www.network-909.de/bassdrum.htm

- Roland TR-909 product/support area:  
  https://www.roland.com/global/support/by_product/rc_tr-909/owners_manuals/

---

## Closing note

The most useful way to develop this instrument is iterative comparison rather than changing many constants at once. Keep the current version as a known-good baseline, record short test passes, alter one parameter group, and compare on both full-range monitors and the actual performance system. The low-frequency body, transient audibility, drive harmonics, and sidechain behavior will translate differently across headphones, Kali monitors, venue systems, modular-level mixers, audio interfaces, and sampler/DJ routing.
