  AZIMUTER - Accessible cassette deck azimuth alignment VST3 plugin

  Version:  1.1
  Author:   Hermis Kasperavičius
  Format:   VST3 (Windows 64-bit; macOS universal, Apple Silicon and Intel)

  WHAT IT IS?
---
Azimuter is a VST3 audio plugin that helps you to align the playback head of a cassette deck by ear. You load it on a stereo track that is monitoring the deck's output, press play on the deck, and turn the head-azimuth screw while listening to the guidance tone the plugin produces. When the tone reaches its highest pitch and sits at the center in the stereo field, the head is aligned.
Unlike visual phase meters and vectorscopes, Azimuter is built around audio cues that work for blind users and sighted users equally well and accessible controls allows blind user to focus every control and interact with the plugin interface by using keyboard.

  HOW THE ALGORITHM WORKS?
---
Head-azimuth misalignment is, physically, an inter-channel time delay between the left and right halves of the playback head's gap. Azimuter measures that delay using GCC-PHAT (Generalised Cross-Correlation with Phase Transform). The estimate is amplitude-independent: vocals, drums, and quiet passages do not change the reading. It needs music, though - something recorded on both tracks at once. Tape hiss alone is no use: the hiss on the left and right tracks comes from different strips of tape, so there is nothing shared between them to measure. In the silent gaps between songs the plugin therefore goes quiet rather than guessing.

But zero delay is NOT always correct azimuth, and this is the single most important thing to understand about the plugin.

A tape can carry an inter-channel delay of its own, printed into the recording before you ever saw it - by a duplicator whose two record gaps were not exactly in line, or because it was dubbed from a source that was itself played back off-azimuth. No playback head angle can remove that. If you turn the screw until the delay reads zero on such a tape, you have tilted the head AWAY from the angle the tape was recorded at, and you have thrown away high frequencies permanently.

This is not a rare corner case. Of the two tapes this plugin was developed against, correct azimuth sat at +54 microseconds on one and -99 microseconds on the other - opposite signs, 153 microseconds apart, on the same deck. Aiming at zero would have cost 2-3.5 dB of the top octave on the first tape and 9-15 dB on the second.

So Azimuter measures a second thing as well: the high-frequency loss itself. A tilted head does not only delay one channel against the other, it also dulls BOTH channels, and that dulling is what your ears notice. The two effects are locked together by the head geometry, and only the second one identifies true azimuth.

The catch is that you cannot just hunt for maximum treble, because the music's own brightness changes constantly. Azimuter separates them using the fact that programme brightness is frequency-flat - when the music gets brighter, every band rises together - while azimuth loss is frequency-scaled, hitting 14 kHz about three times as hard as 7 kHz. Sweep the azimuth control once across its range and the plugin can pick that signature out and tell you the exact reading that corresponds to correct azimuth on THIS tape. That is what the Calibrate button does.

After calibration the plugin shows you the ERROR from that target rather than the raw delay, so everything you already know still applies: zero is the goal, the tone still peaks there, and the stereo cue still tells you which way to turn.

You'll see the result on screen as a number in microseconds, hear it as a pitched tone whose frequency rises as you approach alignment, and feel where to turn from the tone migrating between your left and right ear.

  SYSTEM REQUIREMENTS
---
- Windows 10 or 11, 64-bit, or macOS 10.13 or later. The macOS build is produced automatically and has not yet been tested on a real Mac - reports welcome.
- A VST3-compatible DAW. Tested on Reaper. 
- An audio interface with a stereo input wired to your cassette deck's line-out (or headphone-out, but line-out is cleaner).

  INSTALLATION
---
WINDOWS. Run the Azimuter installer and install. VST3 plugin will be installed to its standard location:
%CommonProgramFiles%\VST3        (typically: C:\Program Files\Common Files\VST3).

MACOS. There is no installer. Unzip the download and copy Azimuter.vst3 into the VST3 folder in your user Library:
~/Library/Audio/Plug-Ins/VST3        (create the VST3 folder if it does not exist)
The macOS build is not signed by Apple, so the first time macOS may refuse to load it. If your DAW does not list Azimuter, open Terminal and run:
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Azimuter.vst3
then restart the DAW. VoiceOver users: speech goes through OSARA when it is installed in REAPER, otherwise through VoiceOver announcements.

After installation, restart your DAW or trigger a VST3 re-scan. In Reaper this is under Options -> Preferences -> Plug-ins -> VST -> Re-scan, but usually rescan happens automatically on next DAW start.

  SETTING IT UP IN YOUR DAW  (Reaper example)
---
1. Connect your cassette deck's line-out to a stereo input on your audio interface. Make sure L of the deck goes to L of the interface and R to R. This matters: the "left side / right side" cue depends on correct wiring.
2. In Reaper, create a new stereo track.
3. Set the track's input to your interface's stereo pair (e.g. "Stereo: Input 1 / Input 2").
4. Arm the track for recording, and enable input monitoring. You should now hear the cassette through Reaper when you press play on the deck.
5. On that track, click FX, search for "Azimuter", and add it.
6. Make sure no other plugin is loaded before Azimuter on the chain. Stereo wideners, mid/side processors, or anything that touches L/R timing will confuse the meter. Azimuter must see the deck's signal unaltered.
7. Press play on the deck. Once music is playing you should hear the guidance tone (not the tape audio) - it starts about a second after the music does. The tape audio is replaced by the tone on this track.

  QUICK START
---
1. Insert a tape, press play.
2. CALIBRATE THIS TAPE. Press the "Calibrate this tape" button, then turn the azimuth control slowly from one end of its travel to the other while the tape plays. The plugin announces how far you have swept; keep going until it says you have enough range (about 150 microseconds), then press the button again to finish. It takes roughly thirty seconds. If you skip this, the plugin aims at zero delay, which is right only if this tape happens to have no skew of its own.
3. Listen to the tone:
- Pitch HIGH and CENTRED  =  aligned. You're done.
- Pitch LOW and PANNED    =  far off; turn the screw.
- Pulsing low tone        =  you've turned well past the lock point.
4. Turn the azimuth screw slowly. Listen for the pitch to RISE and the tone to MIGRATE toward the centre. If the tone is on your right ear, turn the screw in the direction that brings it toward centre; reverse if it moves further right.
5. As you approach lock, sighted users can watch the microsecond number drop toward zero.
6. Once you've found the highest stable pitch with a centred tone, stop turning. Lock the screw if your deck has a locking mechanism.
7. The alignment is done.
A typical alignment from "obviously off" to "Excellent" takes 5-30 seconds once you have a feel for which direction is which. The calibration in step 2 is the part that takes real time, and it has to be redone for each tape.

  THE INTERFACE  -  WHAT EACH PART MEANS
---
NUMERIC READOUT (top of the window)- Reads "Error: X.X us  (Quality, side)" for example "Error: -22.3 us  (Fair alignment, left side)".
- The number is how far the head is from correct azimuth for this tape, in microseconds. Zero is aligned. Before you calibrate, the target is zero delay and the error is just the raw reading.
- The sign tells you which channel leads:
positive  =  right channel leads
negative  =  left channel leads
- The quality bucket coarsens the number into a category shown in the readout. The buckets are set by what the error actually costs you in treble (measured on a real tape at around 14 kHz), and they are deliberately wider than the reading's own wobble, so the quality only changes when the alignment does:
Excellent alignment   |error| < 5 us      loss about 0.02 dB - inaudible. You are done.
Good alignment        |error| < 15 us     loss under 0.2 dB
Fair alignment        |error| < 30 us     loss under 0.7 dB
Poor alignment        |error| < 60 us     loss under 3 dB - audibly dull
Severe alignment      |error| < 100 us
Off-scale             |error| >= 100 us
- The side (left or right) is named in the readout only from Fair upward. Closer than 15 microseconds the error costs under 0.2 dB and its sign is close to the meter's own noise, so naming a side there would only invite you to fiddle.
- WHAT IS SPOKEN WHILE YOU TURN: only one thing - "Excellent alignment." - when you reach it. Nothing else is announced while you align: the tone is your guidance, and a voice describing every step would only talk over it. The quality buckets above still appear in the readout, so you can read it with your screen reader's review or object navigation at any time to hear the full current state. The readout is text, not a Tab stop, so Tab skips it.
- "Excellent alignment" is spoken once the reading has settled inside 5 microseconds for about three quarters of a second. Holding the screw still afterwards stays silent - the meter's own wobble will not make it repeat. If you drift away (clearly outside Excellent for about two seconds) and come back, it is spoken again, so you always know you have returned.
- The calibration sweep is the exception: there the plugin still speaks your progress and the result, because there is no other way to get that information.

CALIBRATION STATUS (below the readout)
Also text, not a Tab stop. Says either "Not calibrated - aiming at zero delay" or "Calibrated: correct azimuth reads X.X us". While a sweep is running it reports how far you have turned and how many samples have been collected, and the main readout shows "(calibrating)" instead of a quality.

CALIBRATE THIS TAPE (button)
Press once to start a sweep; the button then reads "Finish calibration" - press it to finish. See the calibration section below. The setting is remembered with your project, so reopening the session keeps the tape's target - but a NEW TAPE NEEDS A NEW CALIBRATION.

ANALYSIS CUTOFF  (slider, 1000-16000 Hz, default 6000)
Lower edge of the frequency band the algorithm looks at. Frequencies below this are ignored. The default 6 kHz is appropriate for most cassettes; raise it to 8-10 kHz if you have a high-bias tape with strong HF content and want a tighter reading, lower it to 2-4 kHz if a particular tape is very dull (worn, or all-bass material) and the reading becomes noisy. The slider takes effect immediately, so you can sweep it while the tape plays and settle on whatever gives the steadiest reading.

SMOOTHING  (slider, 0.01-2 seconds, default 0.25)
How long the algorithm averages cross-spectrum data before producing a reading. Short values (0.05-0.15 s) make the meter feel responsive when you turn the screw, at the cost of more wobble. Long values (0.5-1.0 s) give a rock-steady reading but lag your screw motion by that long. 0.25 s is a good default for active screwdriver-in-hand alignment.
This slider is the main control you have over how much the reading wobbles: the wobble shrinks roughly by half for every four-fold increase in smoothing. At the 0.25 s default the reading moves around by about 6 microseconds even on a perfectly steady tape; at 2 s, about 2 microseconds. The Excellent announcement is built to cope with the default, so you do not have to change it - but a longer setting makes the on-screen number steadier and the tone's pitch less jittery for the final touch-up. The slider takes effect immediately.

TONE LEVEL  (slider, 0-1, default 0.4)
Loudness of the guidance tone. Adjust to taste against your monitoring level. 

  THE THREE AUDIO CUES  -  WHAT YOU'RE LISTENING FOR
---
The plugin gives you three layered cues at once, plus silence when there is nothing to measure. They are designed to be unambiguously different from each other so you can identify your situation by ear.
1. PITCH    -    distance from lock
2000 Hz (clear midrange) aligned (0 us)
1000 Hz                  about 7.5 us off
 400 Hz                  about 17.5 us off
 200 Hz (low rumble)     25 us or worse
Pitch rises smoothly as you approach lock. Equal screw movements give roughly equal musical intervals, so even fine corrections are audible. Below 25 us the pitch carries all the detail; at 25 us it bottoms out at 200 Hz and the pulse takes over as the coarse cue.

2. STEREO POSITION    -    direction to turn
Tone in your RIGHT ear - right channel is leading; turn screw the way that brings it toward centre.
Tone in your LEFT ear - left channel is leading; turn the other way.
Tone DEAD CENTRE - aligned.
The pan is constant-power, so the perceived loudness stays the same as the tone migrates between ears.

  3. PULSE    -    "you're well past the lock point"
Steady tone - you're within 25 us of lock.
Pulsing ~3 times/second  you're more than 25 us out; back off.
The pulse deepens the further out you are, reaching its full depth at 100 us. So a shallow pulse means "somewhat past lock" and a hard chopping pulse means "a long way past lock" - the difference is worth learning, because it is the only cue that still tells you anything once the pitch has bottomed out.
The pulse is layered on top of pitch and stereo position; it does not replace them. Even at full pulse, the tone never goes fully silent.

4. SILENCE  -  there is nothing to measure: the deck is paused or stopped, you are in the silent gap between two songs, or the tape carries only hiss. The plugin decides this from whether the two channels share any signal, NOT from how loud the input is, so a quiet deck or a low recording level does not silence it. The tone starts about a second after the music begins and stops about a second after it ends - telling real music from hiss takes a moment of listening. If the tone drops out in the middle of a song, that passage has almost nothing in common between the two channels; move to a fuller part of the tape.

  CALIBRATING A TAPE  -  THE MOST IMPORTANT SECTION
---
Every tape needs this, and it takes about thirty seconds.

WHY. Correct azimuth is the head angle the tape was RECORDED at. Zero inter-channel delay only coincides with that if the machine that recorded the tape had its two gaps perfectly in line. Plenty do not, and a tape dubbed from another tape inherits whatever azimuth error that playback had. On such a tape, aligning to zero delay actively tilts your head off true and costs you high frequencies you can never get back.

HOW.
1. Start the tape playing, on a passage with real high-frequency content - cymbals, hi-hats, sibilant vocals. Avoid silence, fade-outs and spoken word.
2. Press "Calibrate this tape".
3. Turn the azimuth control slowly and steadily from one end of its useful travel to the other. Do not stop at what sounds best - sweep straight past it. The plugin needs to see the treble fall away on BOTH sides of the peak to locate it.
4. The plugin announces your progress every 50 microseconds of swept range, and tells you when you have enough (150 microseconds minimum; more is better). Roughly 100 microseconds either side of the eventual answer is ideal.
5. Press the button again (it now reads "Finish calibration"). The plugin reports the answer: "Calibrated. Correct azimuth is at X.X us, plus or minus N. Now align until the tone peaks."
6. Now align normally. The tone peaks at correct azimuth, and the readout shows zero error there.

IF IT FAILS. The plugin tells you why rather than guessing: "Calibration failed: (reason). Swept N microseconds over M samples. Nothing changed." "The azimuth control was not turned far enough" means sweep wider. "Not enough of the sweep was usable" means the sweep ran over silence, a gap between songs, a fade-out, or something with little in common between the channels. Find a continuous stretch of bright music and try again. "Search found no solution" or "degenerate sweep" mean the treble did not change in a way the model could fit - usually a sweep that never crossed the peak; sweep wider, through the best-sounding point and past it. If pressing the button says "Cannot calibrate yet - start playback first", the host has not started audio processing yet; start playback in the DAW (or arm the track with monitoring on) and press it again. Loudness doesn't matter: a quiet deck calibrates exactly the same as a loud one. If a sweep happens to cross a short gap between songs, the gap is simply ignored rather than spoiling the result. A failed calibration changes nothing, so it is always safe to retry.

HOW ACCURATE DOES IT NEED TO BE? Measured on a real tape, staying within 16 microseconds of the target costs less than 0.25 dB, and within 35 microseconds costs 1 dB. The plugin's own reading wobbles by about 6 microseconds at the default smoothing, so it is comfortably more precise than the job requires - once it is aiming at the right number.

ONE CALIBRATION PER TAPE. The answer is a property of the tape, not the deck. Both sides of a tape are usually close but not identical; calibrate each side if you are being careful.

  TIPS FOR CASSETTE RIPPING
---
PER-TAPE ALIGNMENT IS NORMAL. Every cassette has some inter-channel delay baked in from how it was recorded and duplicated. Aligning per tape gives you the cleanest possible HF and stereo for that specific cassette. This is not a workaround; it's the right way to transfer cassettes seriously. For best result, it's always strongly recommended to align both sides of the tape individually.

  WHEN TO TUNE
---
when the tape is playing content with strong high-frequency information: hi-hats, cymbals, sibilant vocals, brushed snare. Tune during those passages if possible.

  TUNE AT THE MIDDLE OF THE SIDE, NOT THE START.
---
This matters far more than it sounds like it should. The lock point drifts steadily as the tape pack moves from one hub to the other, and on a measured C-90 side it moved 55 to 75 microseconds from start to end - roughly 2 to 3 microseconds per minute, in the same direction throughout. The drift is a property of the shell, the pack and the transport, so some tapes show almost none and others show all of it.

The practical consequence: whatever moment you tune at becomes your alignment for the whole side, and the plugin can only show you the lock point for the moment you are in. If you tune during the first track you will be tens of microseconds out by the last one, and the tape will sound audibly duller at the end than it needs to.

So tune at the MIDDLE of the side, not the beginning. Let the tape run to roughly the halfway point, align there, and the error is split between the two ends instead of piling up at one. On a side measured both ways this was worth about 0.7 dB of high end averaged across the whole side. If the transfer really matters, tune per song and rip in sections.

  DON'T CHASE PERFECTION.
---
Mechanical jitter (capstan wow, tape edge variation, head suspension microflex) plus the statistics of the measurement itself keep the reading moving even at ideal azimuth - on a real tape at the 0.25 s default smoothing, by about 6 microseconds. The number on screen will keep breathing; that is the meter, not your screw.
When you hear "Excellent alignment", stop. Within 5 microseconds the treble loss is about 0.02 dB - nothing anyone can hear. Even "Good alignment" costs under 0.2 dB. Aim for the highest stable pitch and the Excellent announcement; do not chase the last microsecond on the number. The drift across a tape side is worth ten times more than it is.

  ONE TAPE AT A TIME.
---
Once aligned, do not touch the screw until you change tapes (or sides, if the deck has separate heads for each direction). A locking screw helps; a small dab of nail polish over the screw after the rip works as a temporary lock.

  REFERENCE TAPES (optional).
---
If you own a calibrated alignment tape (a mono full-track 10 kHz or 6.3 kHz tone), use it to align your deck's "true" position first - Azimuter will read it just like any other tape, but a reference tape has no baked-in asymmetry, so the reading shows your head's actual geometric error. You can then leave the deck at that position for tapes you trust were duplicated well, and only do per-tape alignment for the questionable ones.

TROUBLESHOOTING AND FAQ
---
"Can I use Azimuter outside Reaper?"
- Absolutely! You can use it in every DAW which supports VST3 plugin format.

"The tone is silent."
- The deck isn't producing signal. Press play; check the deck's output cables; confirm Reaper's track meter shows level on input.
- Tone Level slider is at 0 - raise it.
- There is no music to measure: the tape is in a gap between songs, a fade-out, or hiss only. The tone is deliberately silent then, and comes back about a second after the music starts (see SILENCE under the audio cues). Loudness is not the issue - a quiet deck still produces the tone.
- Reaper's track is muted, or the FX is bypassed.

"The reading is jittery / unstable."
- Increase the Smoothing slider to 0.5 s or higher.
- Make sure no plugin sits before Azimuter on the chain.
- If the tape is very dull (bass-only passages, worn high end), lower the Analysis Cutoff to 2000-3000 Hz.
- Some cassettes are simply that wobbly; the wobble is real and tells you something about the tape's condition.

"The tone seems unrelated to which way I turn the screw."
- Check that L/R from the deck are not swapped on the way to the interface. The side cue depends on correct wiring.
- Some decks have a non-monotonic screw (rare). Test by turning a clearly audible amount and confirming the reading changes in a consistent direction. If it doesn't, the screw may be loose mechanically.

ACCESSIBILITY NOTES
---
- All controls are keyboard-navigable. Tab to move between the Calibrate button and the three sliders; arrow keys to adjust slider values. The two readouts are text: read them with your screen reader's review or object navigation.
- Every control and readout has a title and help text visible to screen readers.
- Spoken messages go through your own screen reader, in its own voice. Inside REAPER with OSARA installed they are sent through OSARA, exactly like OSARA's own messages; otherwise through the standard Windows notification that NVDA, JAWS and Narrator all read. Your screen reader's stop-speech key silences them.
- While aligning, the only automatic announcement is "Excellent alignment" on arrival (spoken again only after you have clearly left and come back, never less than 3 seconds apart). Calibration progress and results are always spoken. The full reading is in the readout label whenever you want it.
- The three audio cues (pitch / pulse / stereo position) state are all distinct from one another by design. You should never be unable to tell which state the meter is in by ear alone.

  TECHNICAL DETAILS  (for the curious)
---
Calibration fits a physical model to your sweep. The classical gap-loss law says a head tilted by angle theta loses high frequencies as

    loss(f) = -20 * log10 | sinc( W * tan(theta) * f / v ) |

with W the track width (0.62 mm on a compact cassette) and v the tape speed. The same tilt also produces an inter-channel delay proportional to the spacing between the two gaps (0.92 mm centre to centre), so loss and delay are rigidly linked - which is exactly why the delay is a usable proxy for head position, and exactly why a delay the TAPE brought with it breaks the link.

Over a sweep the plugin fits

    tilt(t,b) = C_b + G(t) - loss( |delay(t) - target| / ratio , f_b )

where C_b is the programme's average spectral shape, G(t) its brightness drifting through the sweep, and `target` and `ratio` are what we want. C_b and G(t) fall out by least squares; the other two come from a grid search. The fit was validated against a sweep whose answer was known independently from a reference recording: it lands within 6 microseconds, which costs under 0.05 dB, and is stable across a wide range of model settings.

The delay estimate itself is a two-stage lock, which is the classical "set a low tone first, then work up" alignment procedure done in one pass. A wide 1-18 kHz band gives an unambiguous coarse answer; the band from the Analysis Cutoff (6 kHz by default) up to 18 kHz then refines it, searched only within half a carrier period of the coarse result so it cannot jump to a neighbouring correlation lobe. False peaks are real - on one of the development tapes the response nulls at 14 kHz and then rises 2.5 dB again further out, which is more than enough to fool an ear hunting for maximum treble.


- Analysis: GCC-PHAT with 4096-sample windowed FFT, hop 1024 samples, Welch-style cross-spectrum averaging, parabolic peak interpolation for sub-sample resolution.
- Latency: zero added latency through the signal chain; the plugin does not buffer audio between input and output. Analysis runs on its own internal pipeline.
- CPU: light. Typical usage well under 1% on a modern CPU.
- Sample rates: any rate the host runs at. So far tested at 44.1 kHz only.
- Bus layout: stereo in, stereo out. Mono input is not supported -- the plugin needs two channels to measure inter-channel delay.

  Developer info
---
The Azimuter VST3 plugin is developed by Hermis Kasperavičius in 2026.
If you have feature requests, suggestions or bug reports, you can contact developer.
E-mail:
hermis.kasperavicius@gmail.com
website:
http://hermisk.lt
telegram:
t.me/hermis501
facebook:
facebook.com/hermis501.new

---End of the ReadMe---
