![snes2DHD — original HD-2D concept art](./assets/branding/banner.png)

# snes2DHD

An open-source SNES emulator being built to present unmodified games in an HD-2D style.

**Built on [ares](https://github.com/ares-emulator/ares).** snes2DHD uses the ares SNES emulation core, developed by the ares team, Near and other contributors. Our work builds on their emulator rather than reimplementing SNES emulation.

**Pre-alpha — M1 (It plays) in progress. Nothing playable yet.**

Built so far: the vendored ares SNES core, a headless test harness with frame-hash tests on licensed homebrew ROMs, an SDL3 window showing stock 2D frames, and Linux packaging with technical-preview builds. Still missing for M1: input, audio, save states and the tap/FrameState capture. There is no HD-2D renderer yet.

The banner is original concept art, not emulator output.

## What HD-2D means here

The planned renderer combines **flat**, Octopath-like pixel sprites and layers in lit 3D scenes with **voxel**, 3dSen-like extruded tiles and sprites. Per-game profiles will choose and mix both styles within a game, tuning geometry, lighting and camera placement. **No profile = stock 2D.**

Our first showcase targets are **Terranigma (PAL/EU)** and **Chrono Trigger (US)** — comparison shots coming in M3.

## Play

Bring your own legally obtained ROM. Linux technical previews are on the [releases page](https://github.com/nissifield/snes2DHD/releases). Each preview is a prerelease named `nightly-<sha12>` after the commit it was built from, and contains a Linux binary tarball, the matching source tarball and `SHA256SUMS`.

Technical previews are not playable: there is no input, audio or save-state support yet. The Linux binary needs a recent glibc (Fedora 44 era) and SDL3 from your distribution; it is not a portable build. There are no Windows builds.

When playable builds arrive, follow the [playtesting guide](./docs/playtesting.md) to compare profile output with stock 2D and report problems.

## Contribute in 2 minutes

Pick a small task card, paste its prompt into your AI tool, and open a PR. Coding agents and browser chats are both welcome: [start here](./CONTRIBUTE.md).

## Planned architecture

```mermaid
flowchart TD
    ROM[Your ROM] --> Core["ares SNES core — pinned, small tap patch set"]
    Core -->|"PPU register and VRAM/OAM/CGRAM writes with scanlines; selected WRAM bytes"| Tap[Tap adapter]
    Tap --> Frame["FrameState — plain, serialisable data"]
    Frame --> Builder[Scene builder]
    Profile[JSON profile] --> Builder
    Builder --> Scene["Scene — planes, billboards, voxel meshes, lights, camera"]
    Scene --> GPU["SDL3 GPU renderer — Vulkan / D3D12 / Metal"]
    GPU --> FX["Post FX — depth of field, bloom, tilt-shift, shadows"]
    FX --> Window[SDL3 window]
    Input[SDL3 input] --> Core
    Core -->|Audio directly| Audio[SDL3 audio]
```

Real-game FrameState dumps stay local. Repository test fixtures will use synthetic data only.

## Roadmap

| Milestone | Planned outcome |
| --- | --- |
| M0 — Repo & agent team | Contribution docs, task cards, review and CI setup |
| M1 — It plays | ares core, stock 2D frontend, Linux technical previews |
| M2 — Flat HD-2D | Planes, billboards, lighting and post effects |
| M3 — Showcase profiles v1 | Terranigma and Chrono Trigger opening areas; comparison shots |
| M4 — Voxel mode | Voxel geometry mixed with flat elements |
| M5 — Profile inspector | In-app profile editing |

See [ROADMAP.md](./ROADMAP.md) for milestone outcomes and [docs/design.md](./docs/design.md) for the full design.

## Licences and credits

Project-original code is **AGPL-3.0-or-later**; profiles and project-original assets are **CC BY-SA 4.0**. See [NOTICE](./NOTICE) for the licence map and third-party notices.

- **[ares](https://github.com/ares-emulator/ares) — ISC:** the SNES emulation core, developed by the ares team, Near and other contributors, vendored at a pinned commit. Its copyright and licence notices are preserved in [external/ares/LICENSE](./external/ares/LICENSE).
- **ponytail — MIT:** the project's minimal-code contribution rules.
- **Inspiration:** 3dSen by Geod Studio and Octopath Traveler's HD-2D look.

snes2DHD is not affiliated with Nintendo, Square Enix or Quintet.
