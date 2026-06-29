# NAM Platform Pedal Website Design

## Goal

Create the authoritative public website for the NAM Platform Pedal. The site must give players and builders equally direct paths to understand the device, operate it through an accurate browser simulation, build and flash the firmware, and wire every connection that is defined by this repository.

The site will be hosted on GitHub Pages and deployed by GitHub Actions.

## Scope

The first release includes:

- A product and feature landing page.
- A complete user manual with an interactive, control-level pedal simulator.
- Developer documentation for architecture, building, flashing, data packaging, testing, and contribution.
- Hardware documentation for every repository-defined connection, assembly check, and bring-up step.
- Pull-request validation and deployment to GitHub Pages from `main`.
- A concise repository README that points to the authoritative website.

The simulator does not process audio and will not request microphone access. The site will not invent a circuit, BOM, or mechanical design that the repository does not define.

## Technical Approach

Use a handwritten static site in `site/`:

```text
site/
├── index.html
├── manual.html
├── developers.html
├── hardware.html
├── 404.html
└── assets/
    ├── site.css
    ├── site.js
    └── simulator.js
```

No package manager, framework, or site generator is required. Pages use relative URLs so the output works at a GitHub Pages project path. The small amount of repeated navigation markup is preferable to introducing a build system solely for includes.

## Information Architecture

### Landing page

The homepage gives players and builders equal weight:

1. Stage Tech hero with two primary actions: **Try the pedal** and **Build your own**.
2. Concise proof points grounded in the firmware, such as 48 kHz audio, five live controls, QSPI storage, and open firmware.
3. Interactive-pedal preview leading to the manual.
4. Paired player and builder entry cards.
5. Accurate signal path: input gain → noise gate → compressor → NAM → IR → EQ → delay → output volume.
6. Project/repository footer.

### User manual

The manual contains:

- Quick start and connection assumptions.
- Signal-flow explanation.
- Physical control reference.
- Performance, browse, edit, and tuner modes.
- Preset navigation, live edits, save, revert, apply, and cancel behavior.
- Display indicators, dirty state, bypass, and overload warning.
- Symptom-based operating troubleshooting.
- The interactive pedal, followed by matching written instructions.

### Developer guide

The developer guide contains:

- Repository map and supported toolchain.
- Audio callback and main-loop responsibilities.
- DSP chain and real-time constraints.
- UI/control state flow.
- QSPI memory map and data structures.
- Supported NAM model path and limitations.
- Model, IR, and preset data-image creation and inspection.
- Firmware and data flashing workflows.
- USB bootloader command flow.
- Host tests, firmware CI, Pages CI, and contribution workflow.
- Links to canonical source files where implementation detail matters.

### Hardware guide

The hardware guide contains:

- A system overview and Daisy Seed orientation reference.
- A verified table sourced from `HardwareConfig.h` for all five encoders, both footswitches, and the ST7789 display.
- Fixed Daisy audio codec, QSPI, and USB resources.
- Active-low behavior, internal pull-ups, shared ground, signal names, and connection notes.
- Assembly order, continuity checks, a pre-power checklist, staged bring-up, and symptom-based troubleshooting.
- Clear distinction between project-defined connections and missing physical-design inputs.

The current `docs/HARDWARE.md` content is stale: it describes encoder 2 as optional and assigns pins that conflict with the current five-encoder firmware. The website will use `HardwareConfig.h` as the source of truth, and stale duplicate instructions will be removed or reduced to a pointer to the authoritative site.

The repository currently does not define a complete analog I/O and power circuit, a bill of materials, or enclosure/mechanical drawings. The hardware guide must state that these inputs are required for a fully reproducible physical assembly and must not substitute guessed values or wiring.

## Visual Design

Use the approved **Stage Tech** direction:

- Near-black surfaces with restrained cyan, orange, green, and red status accents taken from the firmware display.
- Strong, compact typography and technical labels without a generic documentation-template appearance.
- High contrast, clear section numbering, and generous spacing around dense technical content.
- Device display graphics and signal-flow lines as the primary visual motifs.
- Responsive layouts that retain the same hierarchy on desktop and mobile.

The physical pedal silhouette is consistent throughout the site:

- Five encoders in one row across the top: Gain, Bass, Mid, Treble, Volume.
- A centered portrait 240×320 display.
- Two footswitches across the bottom, presented as previous/revert on the left and next/save on the right.

## Interactive Pedal

The simulator reproduces control and display behavior, not audio processing.

### Inputs

- Encoders accept pointer drag, mouse wheel, and keyboard input.
- The primary Gain encoder also accepts click and hold actions.
- Footswitches distinguish tap, individual hold, and both-switch hold.
- Every input has a visible focus state, an accessible name, and a touch target suitable for mobile use.

### State and behavior

One small state machine owns the demo presets, active preset, saved values, working edit values, current mode, dirty state, and tuner demonstration.

It mirrors current firmware behavior:

- Performance encoders adjust Gain `[0, 2]`, Bass/Mid/Treble `[-12, 12] dB`, and Volume `[0, 1]` using firmware step sizes.
- Primary encoder click opens preset browse.
- Primary encoder hold while browsing opens the highlighted preset editor.
- Browse rotation moves the bounded cursor; click selects; either footswitch cancels.
- Edit navigation and value editing follow the firmware's eight fields: model, cab, input gain, output volume, bypass, bass frequency, mid frequency, and treble frequency.
- FS1/right tap applies and saves edit-mode changes. FS2/left tap cancels them.
- In performance mode, FS1/right tap selects the next preset and FS2/left tap selects the previous preset.
- In performance mode, FS1/right hold saves live values and FS2/left hold reverts them.
- Holding both footswitches enters or exits tuner mode.
- The tuner uses a deterministic demonstration signal and makes clear that it is a UI demonstration, not microphone analysis.
- Reset restores the original demo state.

An adjacent **What can I do here?** panel lists only actions valid in the current mode. The written manual uses the same terms and sequence as the simulator.

Invalid or unavailable actions do nothing. State changes remain in memory and are discarded on page reload.

## Accessibility and Progressive Enhancement

- Documentation is readable and navigable without JavaScript; only the simulator requires it.
- Use semantic landmarks, a logical heading order, skip navigation, and descriptive link text.
- All controls are keyboard-operable and expose their value and action to assistive technology.
- Color is never the only status cue.
- Focus indicators are always visible.
- Text and controls meet WCAG AA contrast and target-size expectations.
- Motion respects `prefers-reduced-motion`.
- The responsive pedal remains usable on narrow touch screens without horizontal page scrolling.

## Validation

Use only runtimes already available on GitHub-hosted runners:

- A Python standard-library checker validates internal links, fragment targets, required pages and sections, duplicate IDs, and relative asset references.
- `node --check` validates JavaScript syntax.
- Node's built-in test runner exercises the simulator's pure state transitions: preset bounds, dirty/save/revert behavior, browse/select/cancel, edit/apply/cancel, value limits, and tuner entry/exit.
- Manual browser checks cover desktop and mobile layouts, keyboard operation, touch-sized controls, JavaScript-disabled documentation, and the GitHub Pages project path.

## Continuous Integration and Deployment

Keep the existing firmware workflow unchanged. Add a separate GitHub Pages workflow that:

- Runs validation on pull requests that affect the site, its checker, or the Pages workflow.
- Runs validation and deployment after matching changes land on `main`.
- Supports `workflow_dispatch` for manual deployment.
- Uses official GitHub Pages configure, artifact upload, and deployment actions.
- Applies least-privilege Pages and OIDC permissions only to the deploy job.
- Uses GitHub's Pages concurrency pattern to prevent stale deployments.

The workflow deploys `site/` directly; there is no generated output to cache or commit.

## Error Handling

- The custom 404 page links to the homepage, manual, developer guide, and hardware guide.
- Missing optional JavaScript leaves a clear simulator-unavailable message while retaining the full written manual.
- Simulator reset provides recovery from any confusing demo state.
- External references are labeled as external and do not replace local instructions needed to complete a documented task.
- Hardware steps stop at any point where required circuit, BOM, voltage, or mechanical data is not defined by the project; the site names the missing input instead of guessing.

## Acceptance Criteria

- The four primary pages and custom 404 page deploy successfully to the repository's GitHub Pages project URL.
- Players can discover features, operate every documented firmware control in the simulator, and complete normal preset, edit, save/revert, and tuner flows.
- Developers can build and flash firmware and data using only the documented sequence and repository prerequisites.
- Every current project-defined hardware connection appears consistently in the pin table and diagrams.
- Missing physical-design inputs are explicit and cannot be mistaken for complete assembly instructions.
- Documentation remains useful without JavaScript and the simulator is operable by keyboard and touch.
- All automated site checks and existing firmware CI pass.

