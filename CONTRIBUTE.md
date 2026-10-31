# Contribute in 2 minutes

Use any AI tool you already have. Start with one small task card:

1. Pick a [`card:ready` issue](https://github.com/nissifield/snes2DHD/issues?q=is%3Aissue%20is%3Aopen%20label%3Acard%3Aready) and comment `/claim`.
2. Paste the starter prompt below, the card URL and the card's **Prompt** into your AI tool.
3. Open a PR to `main` with `Closes #N` (your card number), the Done-when output and this checked rights statement:

- [x] I have the right to submit this under the project's licences and it contains no ROM data or copied code.

**Universal starter prompt**

```text
Help me complete the snes2DHD task card whose URL I provide with this prompt.
Read https://github.com/nissifield/snes2DHD/blob/main/AGENTS.md and the card,
including its Prompt, and follow them exactly. If you cannot open URLs, ask
me to paste AGENTS.md and the card text before starting. Work only on that
card and its allowed files. Follow AGENTS.md section 5 for your tool: a coding
agent works in my fork, runs the Done-when checks and opens a PR; a browser
chat returns every changed file in full, a PR title/body and submission steps.
```

## Using a coding agent

Run it inside a clone of your fork. It creates a branch, completes the card, runs its checks and opens the PR. See [AGENTS.md §5(a)](./AGENTS.md#5-handing-back-your-work).

## Using a browser chat

It returns complete files and a PR title/body for you to submit. See [AGENTS.md §5(b)](./AGENTS.md#5-handing-back-your-work) and the six steps in the appendix below.

## What happens next

The Reviewer agent checks your PR against the card. CI must pass before auto-merge; changes to protected paths wait for a core member. This is the workflow being set up in M0.

## Playtesting

Prefer playing to editing? Follow the [playtesting guide](./docs/playtesting.md) once nightlies arrive in M1. **Profile cards need your own ROM and a local build.** Images and other binary files are welcome but always wait for a core member (PNG or SVG only). Real-game FrameState dumps stay local; never share or commit them.

## Appendix

### Submit files with GitHub's web editor

1. Open your fork of [`nissifield/snes2DHD`](https://github.com/nissifield/snes2DHD).
2. For each file, go to its folder and use **Add file → Create new file**, or open an existing file and click the pencil icon.
3. Paste the path and the full content supplied by your AI tool.
4. Choose **Create a new branch** and commit. Put all files for the card on that same branch.
5. Click **Compare & pull request** and target the upstream repository's `main` branch.
6. Paste the PR title and body, then click **Create pull request**. Check that the body includes `Closes #N`, the rights statement above and your Done-when output.
