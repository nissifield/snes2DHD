# tools/review — review queue runner

`review_queue.py` runs the `.agents/reviewer.md` role over the open queue on a
core member's machine:

- **PR mode** (default): open PRs against `main` with a green `checks`
  status, no protected path, no binary file, a linked
  `card:ready`/`card:claimed` issue, and no review by you at the current head
  SHA. Each PR head is fetched from `origin` (which must be the main repo, not
  a fork) into `refs/review/<n>`; the file checks, scan and the agent's diff
  all use that pinned commit. Nothing is checked out or run. On `APPROVE` it
  posts an approving review and enables squash auto-merge; on
  `REQUEST_CHANGES` it posts the findings. If the head SHA moves between
  review and posting, the PR is skipped.
- **Card mode** (`--cards`): issues labelled `card:draft`. `CARD_OK` swaps the
  label to `card:ready`; `CARD_FIX` comments the findings.

**Dry run by default** — it prints what it would post and posts nothing. Pass
`--live` to actually post. The agent command comes after `--`; the review
prompt is piped to it on stdin.

**`--live` acts as the snes2DHD Chores GitHub App**, not your login: every
`gh` call gets a short-lived installation token signed with the app key at
`~/.config/snes2dhd/chores-app.pem` (outside the repo, owner-managed). If no
token can be minted it exits non-zero and posts nothing. Dry run needs no key.
The key must be mode 600.

The agent reads untrusted diffs, so give it no access to `~/.config/` (the app
key and your gh login live there); see the first example. As a backstop the
runner refuses to post output that looks like a key or token.

## Examples

    python3 tools/review/review_queue.py -- claude -p --permission-mode plan --disallowedTools 'Read(~/.config/**)'
    python3 tools/review/review_queue.py --live -- codex exec --sandbox read-only -
    python3 tools/review/review_queue.py --cards -- kimi -p
