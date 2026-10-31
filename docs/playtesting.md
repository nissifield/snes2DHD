# Playtesting

**Nothing is playable at M0.** Nightly builds start in M1; profile comparisons become useful when profiles arrive in M3. These steps describe that planned workflow.

## Get the nightly

Download the build for your system from the rolling [nightly release](https://github.com/nissifield/snes2DHD/releases/tag/nightly), available from M1. Bring your own legally obtained ROM.

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
