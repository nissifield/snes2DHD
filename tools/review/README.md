# tools/review — review queue runner

`review_queue.py` runs the `.agents/reviewer.md` role over the open queue on a
core member's machine:

- **PR mode** (default): open PRs with a green `checks` status, no protected
  path, no binary file, a linked `card:ready`/`card:claimed` issue, and no
  review by you at the current head SHA. On `APPROVE` it posts an approving
  review and enables squash auto-merge; on `REQUEST_CHANGES` it posts the
  findings. If the head SHA moves between review and posting, the PR is
  skipped.
- **Card mode** (`--cards`): issues labelled `card:draft`. `CARD_OK` swaps the
  label to `card:ready`; `CARD_FIX` comments the findings.

**Dry run by default** — it prints what it would post and posts nothing. Pass
`--live` to actually post. The agent command comes after `--`; the review
prompt is piped to it on stdin.

## Examples

    python3 tools/review/review_queue.py -- claude -p --permission-mode plan
    python3 tools/review/review_queue.py --live -- codex exec --sandbox read-only -
    python3 tools/review/review_queue.py --cards -- kimi -p
