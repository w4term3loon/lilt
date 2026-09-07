# Validation record

These are historical development measurements. The 0.5.0 release preparation
checks are recorded at the end of this document. Earlier speed measurements use
machine-optimized builds and do not establish the speed of portable releases.

7 September 2026. Ubuntu 24.04.4, GNOME 46, Wayland, Intel Core Ultra 7 268V
(8 cores), 32 GB RAM. Release build, CPU only, four inference threads.

- Native C++ build and engine tests passed.
- Preferences rendered and visually inspected under isolated Xvfb.
- Application D-Bus tests passed: single instance, rejects non-Shell attachment,
  no recording without integration, repeated cancellation/restart, and insertion
  errors remain visible after queued worker callbacks.
- The app used about **44 MiB RSS** with preferences open in that isolated test.
  That excludes the existing desktop and is not a universal memory estimate.
- A real inference run on whisper.cpp's 11-second JFK sample, using the downloaded
  and SHA-256 verified small Q5_1 model, took **3.52 seconds wall time** and peaked
  at **361,848 KiB RSS (353 MiB)**. English was selected for this run. This includes
  model loading; it does not measure microphone or desktop insertion latency.
- Output was: “And so my fellow Americans, ask not what your country can do for
  you, ask what you can do for your country.”
- This single sample establishes that the local engine works, not an accuracy
  benchmark across accents, noise, languages, or applications.

The first-load extension activation on the user's normal Wayland login still
requires logout/login. No logout is performed automatically.

Desktop integration was also exercised in a separate GNOME 46 Wayland Shell,
using a real GTK entry and a fake transcription service. The complete string
`Árvíztűrő tükörfúrógép 🦉 Hello.` was inserted exactly, the entry's Return/activate
count stayed at zero, and Escape cancelled the next session. A held Enter stayed
captured through receipt of the transcript and was released before insertion.
This verifies that input path; it does not establish compatibility with every
application or the separate IBus/XWayland path.

A synthetic audio capture test routed the JFK sample through a uniquely named
PulseAudio null-sink monitor. It produced the expected sentence in **3.782 seconds
after Stop**, without opening the real microphone or changing global audio
defaults. Cancelling a second run during inference emitted no transcript. The
in-flight encoder took about **2.7 seconds** to yield after cancellation; the UI
can cancel immediately, but another recording waits for that worker to finish.
The temporary audio device was removed afterward.

The packaged GNOME harness was rerun after event-order fixes and passed using the
actual Ctrl+Super+Space shortcut. Eight session regressions and five text-boundary
cases also passed. The installed executable and extension match the tested build;
the preferences service started on the normal session with no logged warnings.

## Startup correction (0.1.1 / extension 2)

A normal login exposed two gaps in the original tests: the proxy could not start
an absent native service, and the native interface was exported after its bus
name had already become visible. The proxy now uses
`DO_NOT_AUTO_START_AT_CONSTRUCTION` so actual actions can activate the app. The
native interface is exported in GApplication's `dbus_register` hook before name
acquisition. Shortcut validation now waits for GTK's display initialization.

The new isolated activation regression uses the actual extension proxy/Gio calls
and the actual C++ service, including saved settings. It passes eleven service
activations: initial startup, two reopenings after Quit, and eight immediate
GetAll/Attach/Quit cycles. Reverting only the proxy flag reproduces the reported
“without an owner” error; the original executable reproduces “No such interface”.
The corrected build has no GTK/GLib warnings in these tests. Native UI smoke,
engine tests, and the GNOME Wayland input test also pass. CTest includes the
activation regression when its test tools are available.

The updated executable was installed and restarted successfully in the normal
session, preserving the user's configured shortcut. GNOME 46 caches imported
extension code, so the installed extension update is loaded at the next login;
the existing extension works with the explicitly restarted service meanwhile.

Primary API references: [proxy activation flags](https://docs.gtk.org/gio/flags.DBusProxyFlags.html),
[registering the interface before owning the name](https://docs.gtk.org/gio/vfunc.Application.dbus_register.html),
and [GNOME 46 extension module caching](https://github.com/GNOME/gnome-shell/blob/46.0/js/ui/extensionSystem.js).

## Minimal indicator and finish shortcut (0.2.0 / extension 3)

The recording pill is now 63×37 logical pixels with nine rounded bars driven by
the existing 10 Hz audio-level signal. Loading/transcription has a subtle lavender
pulse. All animation/timers stop on idle and extension disable; there is no new
inference or audio-processing dependency. Screenshots from a private GNOME 46
session were visually checked.

Preferences exposes separate Start and Finish capture buttons. Missing existing
`finish_shortcut` settings default to Return; Escape stays reserved for cancel.
An actual GTK dialog test under private Xvfb set Finish to Ctrl+F8, restored
Enter, rejected a bare global Start key, and verified independent persistence.
The native activation test also verifies a configured Shift+F8 finish binding
survives service restarts and is exposed immediately over D-Bus.

Nineteen session regressions and the private GNOME 46 integration test pass.
They cover custom Ctrl+F8 (unmodified Enter/F8 do not finish), held modifiers,
finishing with Start, Caps/NumLock tolerance, Unicode insertion, cancellation,
and zero unintended Return activation. The new indicator and finish handling
load on the next login because GNOME caches the extension module.

## Background model initialization (0.2.1)

Whisper initialization now starts on a separate thread after microphone readiness
and overlaps capture. The final decoder reuses that one context. Model memory is
still released after the session, before the terminal state callback.

Build and CTest passed (2/2). The opt-in synthetic null-sink integration also
passed. It uses Whisper's logging callback to block initialization deliberately
and proves that audio level updates continue during the block, cancellation
closes the recording stream before releasing the initializer, a corrupt model
stops capture without Finish, immediate Stop from the recording callback skips
loading safely, and normal decoding initializes the model exactly once.

The successful 11-second JFK capture produced the expected sentence in 3.473
seconds after Stop in this run. Cancellation during decoding emitted no text.
These timings are individual runs, not evidence of a statistically measured
speedup. No real microphone was recorded; the temporary monitor sink was removed.
PipeWire stream removal is asynchronous, so the test waits up to one second for
server metadata to reflect teardown, while retaining the separate test that
capture closes before a blocked initializer is released.

The independent [live transcription investigation](live-transcription.md) records
cumulative-prefix decoding timings and a separate GNOME/GTK provisional-text
prototype. Real-time text insertion is not enabled in the production extension.

## Live preview and Medium models (0.3.0 / extension 4)

The engine now emits provisional hypotheses during capture. A separate worker
loads the model once and serializes repeated full-prefix inference; a single
pending snapshot is replaced with newer audio. Stable-prefix byte offsets mark
complete words shared by consecutive hypotheses, and can shrink on correction.
Final insertion still occurs only after Finish. Background learning remains
deferred; no audio history or training pipeline was introduced.

The C++ build and CTest passed (2/2). Unit coverage includes corrected and
shrinking stable prefixes, UTF-8 boundaries, latest-only coalescing, dropping
pending work on Finish, and waking a decoder waiting for its cadence timer.
An opt-in null-sink capture test using Small produced three nonempty previews
before Finish, zero premature final results, the expected JFK final sentence,
and exactly one model initialization. Existing blocked-load/capture-overlap,
load failure, immediate Stop, and cancellation checks also passed. No real
microphone was used; the temporary sink was removed afterward.

That live-enabled run took 5.523 seconds after Finish. Finish must wait for a
running partial encoder to yield before final decoding, so preview can increase
this delay. Disabling Live preview retains the preload-only behavior. Cancellation
of the separate opt-out decoding run emitted no text and took 2.861 seconds.

The native D-Bus/UI smoke passed without GTK/GLib warnings. A real GTK interaction
test under private Xvfb verified the live-preview switch changes and persists,
Medium can be selected and persisted, and independent Start/Finish dialogs still
work. Both Small and Medium preferences layouts were visually inspected. The
open preferences process used about 44 MiB RSS.

Medium multilingual Q5_0 weights were downloaded using pinned size and SHA-256
verification. Separate CLI runs on the same 11-second JFK sample measured:

| Model | Process wall time | Peak RSS |
| --- | ---: | ---: |
| Base English Q5_1 | 1.319 s | 162,796 KiB (159 MiB) |
| Medium Q5_0 | 9.899 s | 847,168 KiB (827 MiB) |

Both recognized the sample's words correctly with different punctuation. These
single CPU runs include loading, exclude microphone/desktop latency, and are not
an accuracy ranking. The user's existing Base English selection and shortcuts
were preserved; Medium is an available alternative in Preferences.

Extension text/geometry and session regressions passed, followed by an isolated
GNOME 46 session with a real GTK Wayland entry and synthetic transcript signals.
The field remained empty during preview, received exact Unicode text on Finish,
and its Return/activation count stayed zero. Held modifiers, finishing via Start,
late results after Escape, and removal of preview/grab/timers were verified.
Light and dark screenshots were visually inspected; the long dark preview was
420×70 logical pixels and stayed inside the monitor work area. The empty test
entry reported zero-height caret geometry, exercising the window-edge fallback.
This does not establish caret support in every application or inline composition.

The independent native lifecycle review found no actionable issues in callback
lifetime, stale generation/owner suppression, decoder shutdown, queue coalescing,
or preview setting persistence. Extension 4 requires the next logout/login to
replace GNOME's cached module; no automatic logout is performed.

## In-field live text and English-only preferences (0.4.0 / extension 5)

The separate text bubble is removed. A temporary IBus engine now supplies
replaceable composition inside the focused field, while the existing bottom
waveform remains. Finish commits the final transcript once after key release;
Escape clears the draft. The app remains C++ with the existing GNOME/GJS
integration: no additional inference backend or native build dependency was
introduced.

The native build and CTest passed (2/2). The activation regression verifies
legacy multilingual settings migrate to the same model size in English, saves
`language=en`, and preserves both shortcuts and the live-text preference. The
model downloader now offers only English weights and defaults to Small English.
The user's existing Base English model remains usable without a download.

An isolated Xvfb smoke test passed with no GTK/GLib warnings. The preferences
window was visually inspected at approximately 350×290 pixels: Model, Start,
Finish, and Live text only. The language selector, slogans, idle message,
installed-model metadata, explanatory footer, and extra hide button are removed.
Download/setup/errors and selectable recovery text appear only when needed.

The production extension passed complete GTK integration suites on three paths:
native Wayland text input, the GTK IBus module on Wayland, and GTK on XWayland.
Each run verified real preedit revisions, literal markup/Unicode, preserved
selection until final replacement, custom Ctrl+F8, default Enter with repeat,
finishing via Start, held modifier releases, exactly one final insertion, zero
Entry activation, Escape, focus loss, target closure, and input-engine cleanup.
Switching to English GB during dictation cancelled it and preserved that explicit
input-source choice. No Shell grab or preview bubble exists during live sessions.

IBus GTK screenshots showed the tentative suffix in gray inside the field.
Native Wayland retained the application's composition underline. Color styling
depends on the client; arbitrary application compatibility is not assumed.

Real testing exposed and resolved three IBus/GJS integration details: unsafe GI
marshalling for serialized component variants, asynchronous discovery of FocusId
support (initial legacy FocusIn must be handled), and IBus 1.5.29 clearing a normal
xkb engine when a temporary component disconnects. Cleanup now waits for daemon
acknowledgment of the component's disconnection, then restores the saved engine
only if no valid replacement is already selected. Final text and preedit clear
signals are explicitly flushed before closing the connection, with a bounded
cancellable timeout. [Gio flush/close semantics](https://docs.gtk.org/gio/method.DBusConnection.close.html)

Session regressions also cover late idle properties during input-method startup,
stale results, duplicate focus cancellation, and unmatched modifier releases.
The native engine's audio/inference algorithm was unchanged in this release.
All GUI/input-method tests used private D-Bus, XDG, and display sessions with
synthetic recognition; no user microphone or live desktop input method was used.

The prototype still used a temporary name at this point. The project was named
lilt during the subsequent 0.5.0 release preparation. Learning remains deferred.

A separate Chromium 152 X11 probe passed against the production extension and
synthetic recognizer after the final flush change. Both a textarea and a
contenteditable field received real composition updates and one final transcript;
custom/default/Start Finish handling produced zero Enter keydown/submission
events. Escape restored the original textarea and the xkb engine was restored.
Chromium displayed its ordinary composition underline and ignored the gray
foreground attribute. This is evidence for a Chromium input path, not a direct
test of every Electron application or Codex itself. The probe used a private
browser profile and the isolated compositor's X11 display.

The complete GTK IBus integration suite was also rerun successfully after the
flush change, including cancellation and input-source takeover. Native build,
CTest (2/2), Python/shell syntax, GJS helpers/session regressions, and the module
import check passed on the final source.

## lilt release preparation (0.5.0 / extension 6)

The project is named lilt. Legacy names remain only in migration code, its tests,
and migration documentation. The remote repository location is deliberately
unset, and the extension UUID remains `lilt@local` until a public namespace is
chosen. No GitHub push or publication is part of this preparation.

The extension's D-Bus initialization and method completions are scoped to their
enable/session generation. A regression exercises rapid disable/re-enable and
checks that obsolete connections retain no handlers. A second regression covers
IBus panel destruction/reconnection; the real compositor tests now reject
JavaScript errors and disposed-object critical messages in their logs.

Production shortcut handling uses IBus keysym APIs instead of Gdk. The regression
compares 2,244 GTK key names, case folding, and multimedia aliases with the native
preferences behavior. The text/session/lifecycle suites and strict GSettings
schema validation pass. Complete private GNOME 46 Wayland, GTK/IBus, and XWayland
suites pass with synthetic recognition and no user microphone access.

Native migration tests verify non-destructive import of old configuration and
models, preservation of existing lilt files, one-time import, recovery after
failure, and cross-filesystem copy fallback. The activation regression verifies
the renamed D-Bus service, English model migration, and retained shortcuts.

Packaging checks cover staged installation, upgrade cleanup, installed-script
uninstallation, optional data purging, custom XDG paths, special-character D-Bus
launch paths, staging containment, relocated `--version`, and installation from
the extracted native archive. The executable has no RPATH/RUNPATH and resolves
runtime libraries without the development sysroot. Release packages use baseline
x86-64 CPU settings and contain no model weights. The matching source archive
includes the pinned whisper.cpp source and configures with FetchContent network
access disabled.

The documentation screenshots are crops from the renamed extension's isolated
GTK/IBus run. CI configuration mirrors the local build/package checks and adds a
GNOME 46 input-path matrix; the remote CI workflow has not run because no remote
repository is configured. These checks do not substitute for testing on another
physical machine or establish portable-build transcription latency.
