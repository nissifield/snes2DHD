# snes2DHD branding

**`logo.svg`** — Original, fully outlined 512 × 512 vector lockup: an amber pixel tile lifts off a mint-topped voxel block, with a small light spark and the exact `snes2DHD` wordmark below. Palette: midnight `#101C2C`, mint `#7CE0C3`, petrol teal `#237B83`, slate `#30475E`, amber `#FFC078`, ivory `#F4F1E7`. The lettering is an original hand-built 3 × 5 pixel alphabet, not a font; no third-party font is used. The SVG has a transparent canvas, no linked resources, and no live text.

**`logo-512.png`** — 512 × 512 RGBA export of the vector lockup, with true transparency outside the mark and its midnight wordmark plate. Palette: `#101C2C`, `#7CE0C3`, `#237B83`, `#30475E`, `#FFC078`, `#F4F1E7`. Lettering: the same original pixel paths; no font or font licence dependency. Intended for project avatars and large branding placements.

**`logo-32.png`** — 32 × 32 RGBA optical export with transparent background; the lifted tile remains recognizable and the complete wordmark is snapped to individual pixels for small-size reading. Palette: `#101C2C`, `#7CE0C3`, `#237B83`, `#30475E`, `#FFC078`, `#F4F1E7`. Lettering: the original pixel alphabet, with no third-party font. The wordmark is positioned on a compact midnight plate for contrast on both light and dark surfaces.

**`banner.png`** — 1600 × 500 README hero: an original cream-hooded cartographer and a rust-orange woodland companion stand beside a lantern bridge, glowing stream, pine trees, and floating voxel terrain. Miniature lighting, bloom, and depth of field express the intended HD-2D presentation. The exact wordmark and “SNES games, reborn in HD-2D” tagline occupy the dark left side. Art-direction palette: midnight `#101C2C`, mint `#7CE0C3`, petrol teal `#237B83`, slate `#30475E`, amber `#FFC078`, ivory `#F4F1E7`; the painted scene includes shaded variations. Wordmark: original pixel paths, no font. Tagline: DejaVu Sans Regular, open-licensed under the Bitstream Vera Font License, with DejaVu additions in the public domain; converted to paths before rasterization.

**`social-preview.png`** — 1280 × 640 GitHub preview with a separate composition of the same original diorama and matching wordmark/tagline. Typography and the island fit inside a 40px inset; the illustration is reduced onto a midnight canvas to protect the island edges from cropping. Art-direction palette: `#101C2C`, `#7CE0C3`, `#237B83`, `#30475E`, `#FFC078`, `#F4F1E7`, plus shaded scene variations. Wordmark: original pixel paths. Tagline: DejaVu Sans Regular, Bitstream Vera Font License with public-domain DejaVu additions, outlined before rasterization.

**`README-branding.md`** — Asset descriptions, exact palette values, typography provenance, and reproducible art-direction prompts. This is plain Markdown with no bundled or prescribed display font; its appearance follows the reader’s Markdown renderer. Palette references are `#101C2C`, `#7CE0C3`, `#237B83`, `#30475E`, `#FFC078`, and `#F4F1E7`.

## Provenance and use

These are original project illustrations and lettering, with no game artwork, company marks, console/controller imagery, or borrowed character designs. The scenery is generated concept art, not emulator output or a claim of demonstrated rendering capability. Both scene backgrounds were made with the built-in image_gen tool (one attempt each); the logo was drawn directly as SVG. Typography was added from vector paths, with final layouts composed and sized using FFmpeg. No external font is required to display any asset. The two logo PNGs preserve alpha; the banner and social preview are opaque RGB.

## Final generation prompts

README hero background — built-in image_gen, 1 attempt:

```text
Use case: stylized-concept. Asset: original snes2DHD README hero background, panoramic 1600x500 composition (3.2:1), generate wide.
A polished miniature HD-2D diorama, a floating square voxel landscape with tiny crisp pixel-art standing sprites, lit 3D terrain, cinematic tilt-shift depth of field and delicate bloom. Entire diorama sits on RIGHT HALF of frame with generous dark EMPTY LEFT HALF for later typesetting. Main subject on right: a small stepped mossy slate island, luminous mint stream flowing diagonally through it, a tiny angular amber lantern on a wooden arch bridge, two entirely original tiny travelers: a cream hooded cartographer with a square teal backpack, and a small rust-orange long-eared woodland creature. Faceted blocky miniature pine trees, a few golden grasses. Clear pixel sprites contrasted with physically lit dimensional blocks. Matte midnight navy background #101C2C, mint #7CE0C3, petrol teal #237B83, slate #30475E, amber #FFC078, ivory #F4F1E7. Subtle soft pool of light under floating island, warm amber key light from upper right, mint stream reflected light. Elegant carefully art-directed composition, rich material detail, sharp central bridge, soft distant trees. Island fully visible with dark breathing room above and below.
NO TEXT, NO letters, NO logos, NO watermark. 100% original scenery and characters; no existing game characters, no company logos, no console or controller imagery, no colored controller buttons, no imitation of any franchise trade dress. This is concept illustration of a rendering style, not emulator screenshot.
```

Social preview background — built-in image_gen, 1 attempt, hero background supplied as a visual reference:

```text
Use case: stylized-concept. Asset: GitHub social preview background, landscape 1280x640, exactly 2:1 aspect ratio.
Image 1 is a visual reference for palette, original characters and materials only. Create a new composition in the SAME original world. A tiny cream hooded cartographer with teal square backpack and rust-orange long-eared forest companion stand at an amber lantern bridge over a mint glowing stream on a FLOATING voxel diorama island with mossy stepped slate rocks and miniature angular pine trees. Pixel-art standing sprites within a lit miniature 3D scene, crisp central details, soft tilt-shift depth of field and restrained bloom. Island placed entirely in RIGHT 58 percent of frame, fills vertical middle 75 percent, all extremities INSIDE 55px margin. EMPTY DARK NAVY LEFT 45 percent for typesetting later, no bright elements on left. Deep midnight #101C2C, mint #7CE0C3, petrol teal #237B83, slate #30475E, amber #FFC078, ivory #F4F1E7. Warm cinematic key light from upper right; dark atmospheric background.
No text, no letters, no watermark, no logos, no console/controller/button imagery. Entirely original characters/scenery; no franchise characters or trade dress. Polished art for open-source emulator branding, concept illustration, not actual game footage.
```


## DejaVu Sans licence notice

The following notice accompanies the outlined tagline lettering (the font binary is not bundled):

```text
Copyright: Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved.
 Bitstream Vera is a trademark of Bitstream, Inc.
 DejaVu changes are in public domain.
License: bitstream-vera
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of the fonts accompanying this license ("Fonts") and associated
 documentation files (the "Font Software"), to reproduce and distribute the
 Font Software, including without limitation the rights to use, copy, merge,
 publish, distribute, and/or sell copies of the Font Software, and to permit
 persons to whom the Font Software is furnished to do so, subject to the
 following conditions:
 .
 The above copyright and trademark notices and this permission notice shall
 be included in all copies of one or more of the Font Software typefaces.
 .
 The Font Software may be modified, altered, or added to, and in particular
 the designs of glyphs or characters in the Fonts may be modified and
 additional glyphs or characters may be added to the Fonts, only if the fonts
 are renamed to names not containing either the words "Bitstream" or the word
 "Vera".
 .
 This License becomes null and void to the extent applicable to Fonts or Font
 Software that has been modified and is distributed under the "Bitstream
 Vera" names.
 .
 The Font Software may be sold as part of a larger software package but no
 copy of one or more of the Font Software typefaces may be sold by itself.
 .
 THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF COPYRIGHT, PATENT,
 TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL BITSTREAM OR THE GNOME
 FOUNDATION BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, INCLUDING
 ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM OTHER DEALINGS IN THE
 FONT SOFTWARE.
 .
 Except as contained in this notice, the names of Gnome, the Gnome
 Foundation, and Bitstream Inc., shall not be used in advertising or
 otherwise to promote the sale, use or other dealings in this Font Software
 without prior written authorization from the Gnome Foundation or Bitstream
 Inc., respectively. For further information, contact: fonts at gnome dot
 org.

```
