# Playtesting

**M1 technical preview — not yet playable.** Available now on Linux: a stock 2D SDL3 window (`snes2dhd`), fixed keyboard input for Controller 1, and the headless test runner (`snes2dhd-headless`). Audio, save states, tap/FrameState capture and HD-2D profiles are not implemented yet; profile comparisons become useful when profiles arrive in M3. The steps below describe that planned workflow.

## Get the technical preview

Download the latest Linux technical preview from the [releases page](https://github.com/nissifield/snes2DHD/releases). Each preview is an immutable prerelease named `nightly-<sha12>` after the commit it was built from, containing a Linux binary tarball (`snes2dhd-linux-x86_64.tar.gz`, requiring a Fedora 44-era glibc and system SDL3), the matching source tarball and `SHA256SUMS`. There are no Windows builds. Bring your own legally obtained ROM.

## Check your ROM revision

Calculate the ROM file's SHA-256 locally. Replace the example path with your own file.

Linux (`sha256sum`):

```sh
sha256sum "/path/to/your-game.sfc"
```

Windows PowerShell (`Get-FileHash`):

```powershell
Get-FileHash -Algorithm SHA256 "C:\Games\your-game.sfc"
```

Compare the full hash locally with the profile's supported ROM hashes to check the revision. For a public report, use **only the first 8 hex characters** of the SHA-256 as the revision hash prefix.

## Compare profile on and off

Once a profile is available, open the game and toggle the profile **on/off** at the same scene. Compare it with stock 2D: look for missing tiles, misplaced sprites, distracting lighting or depth, and anything that makes the game harder to read. Note the profile version you used.

## File a Playtest report

Open the repository's [issue forms](https://github.com/nissifield/snes2DHD/issues/new/choose) and choose **Playtest**. Include:

- **Game**, including region (for example, Terranigma PAL/EU or Chrono Trigger US).
- **Revision hash prefix:** the first 8 hex characters of your ROM's SHA-256 only.
- **Profile version** tested.
- **What looked wrong:** where it happened, how to reach it, and whether toggling the profile off fixed it.
- **Screenshot of the emulator output** showing the problem; attach it to the report, not the repository.

Never attach ROMs or save files. Keep real-game FrameState dumps local too.
