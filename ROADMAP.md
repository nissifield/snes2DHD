# snes2DHD roadmap

Milestones come from the design spec, §9. Each one is done when all of its outcomes hold.

## M0 — Repo & team
- Licences, NOTICE and CODEOWNERS are in place. Protected paths need @nissifield.
- The README skeleton with branding, CONTRIBUTE.md, and the card and playtest issue forms exist.
- AGENTS.md and the PM and Reviewer roles exist. A card can go from draft to ready through Reviewer `CARD_OK`.
- CI `checks` runs the base-branch scanner and card/PR checks. Branch protection and auto-merge are on.
- The claim bot (`/claim`, 7-day expiry) and the Mattermost notifier are running.

## M1 — It plays
- The ares SNES core is vendored at a pinned commit. The tap is a small patch set in `patches/ares/`.
- The tap adapter emits FrameState as plain, serialisable data.
- The SDL3 frontend plays a ROM in plain 2D with audio, input and save states.
- A headless test harness checks frame hashes on open-licensed homebrew ROMs in CI.
- Linux and Windows nightlies are published as the rolling `nightly` prerelease.

## M2 — Flat HD-2D
- The scene builder `(FrameState, Profile) → Scene` is a pure function. With no profile, the picture is identical to stock 2D.
- The SDL3 GPU renderer draws background layers as planes and sprites as billboards.
- It has lighting, shadows, depth of field, bloom and tilt-shift at 60 fps at 1080p on a mid-range GPU.
- Mode 7 renders as a real 3D ground plane.
- Stock 2D stays the default, with an instant A/B toggle.

## M3 — Showcase profiles v1 (first public announcement)
- The profile format and its JSON Schema are validated in CI. Any mismatch falls back to stock rendering for that element.
- There is a FrameState dump and tile-hash tool. Dumps stay local and are never committed (D4).
- A Terranigma (PAL/EU) profile covers Crysta and the first overworld flight.
- A Chrono Trigger (US) profile covers the opening and the Millennial Fair. Its scene selectors are new reverse-engineering work.
- **Beats Super ZSNES:** @nissifield decides this from side-by-side stills of the named scenes (CT: Millennial Fair, Truce, Guardia Forest; Terranigma: Crysta, first overworld flight).
- No game looks worse with its profile on than in stock.

## M4 — Voxel mode
- Tiles and sprites can be extruded into voxel meshes, 3dSen-style.
- Profiles choose flat or voxel per element and can mix both in one game.
- The showcase profiles use voxels where they look better, and are checked by playtest.
- Performance stays at 60 fps at 1080p.

## M5 — Profile inspector
- An in-app editor lets you pause, click a tile or sprite, see its hash, and assign a rule.
- Edits are saved to `profile.json` and pass schema validation.
- Contributors with their own ROM can write profiles without editing JSON by hand.
