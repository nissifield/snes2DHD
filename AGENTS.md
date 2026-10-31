# AGENTS.md — rules for every contributor's AI model

<!-- Ponytail rules © DietrichGebert, MIT; see third_party/ponytail/LICENSE -->

You are helping someone contribute to **snes2DHD**, an open-source SNES emulator that shows unmodified games in an HD-2D style. Read this file and your card. Then do the card. Nothing else.

## 1. Scope
- Work only on the **one card** (GitHub issue) you were given. No side fixes or "while I'm here" changes.
- Change only the files listed under **Files you may touch**. Never change anything under **Files you must not touch**.
- Never edit protected paths: `.github/ .agents/ CODEOWNERS LICENSE* LICENSES/ third_party/ external/ patches/ tools/ci/ tools/review/ tests/golden/ profiles/schema/ ROADMAP.md AGENTS.md .gitattributes .gitmodules .lfsconfig **/CODEOWNERS`. If the card seems to need one, stop and say so.
- If the card is unclear or impossible, stop and explain why. Don't guess.

## 2. Minimal code (ponytail)
Use the first option that fully works:
1. Does it need to exist? Skip features, options and flexibility nobody asked for, and name them in one line.
2. Is it already in this repo? Reuse it the way the surrounding code does.
3. Is it in the standard library or the platform? Use it.
4. Is there an installed dependency for it? Use it. Never add a dependency for a few lines.
5. Otherwise, write the minimum code that works.

- Finish everything the card needs, including the callers, tests and fixtures your change breaks.
- No abstraction, wrapper, option, config or "for later" code nobody asked for. Deleting beats adding. Keep the existing structure.
- Comment only the *why*, in one line. If a shortcut has a known limit, mark it: `shortcut: <the limit>, <when to upgrade>`.
- New non-trivial logic (a branch, a loop, a parser, anything security-related, a new script) gets one small test. Trivial changes need none.
- Never cut validation at trust boundaries, error handling that prevents data loss, security, or accessibility.
- End your reply with one or two lines saying what you skipped or didn't check.

## 3. Never
- ROM data, or any data dumped from a game (including FrameState dumps from real games).
- Redrawn or traced game art.
- Code or data copied from Super ZSNES or from any licence-incompatible project.

Tile-graphics hashes and WRAM addresses in profiles are allowed.

## 4. Licences
Code is **MPL-2.0**. Profiles and assets are **CC BY-SA 4.0**. Submit only work you have the right to license this way.

## 5. Handing back your work

**(a) Coding agents** (Codex, Claude Code, Kimi, …):
1. Work on a new branch in the contributor's fork.
2. Run the card's **Done when** command and check you get the expected output.
3. Open a PR to `main`. The PR body must contain `Closes #N` (the card number) and this exact line:
   `- [x] I have the right to submit this under the project's licences and it contains no ROM data or copied code.`
4. Paste the Done-when output into the PR body.

**(b) Browser chats** (ChatGPT, Claude, Gemini web, …):
1. Reply with every changed file as its **path** followed by its **complete content** in one fenced block. Never send partial snippets or diffs.
2. Then give a **PR title** and a **PR body**. The body includes `Closes #N` and the exact rights line above.
3. Then tell the human how to submit it in the GitHub web editor:
   1. Open their fork of `nissifield/snes2DHD`.
   2. For each file, go to its folder and use **Add file → Create new file**, or open an existing file and click the pencil icon.
   3. Paste the path and the full content.
   4. Choose **Create a new branch** and commit.
   5. Click **Compare & pull request**.
   6. Paste the PR title and body, then click **Create pull request**.

The Reviewer checks your PR against the card. CI must pass before anything merges.
