#!/usr/bin/env bash
# Apply snes2DHD repo settings. Idempotent; run by a repo admin with `gh` logged in.
# Manual (no API): social preview image, Discussion categories Playtest / Core digest / Ideas.
set -euo pipefail
R="${1:-nissifield/snes2DHD}"
cd "$(dirname "$0")/../.."

# Labels first: the issue forms apply them.
python3 -c 'import json; [print(l["name"], l["color"], l["description"], sep="\t") for l in json.load(open(".github/labels.json"))]' |
while IFS=$'\t' read -r name color desc; do
  gh label create "$name" --repo "$R" --color "$color" --description "$desc" --force
done

gh api -X PATCH "repos/$R" --silent \
  -F has_discussions=true -F has_wiki=false -F has_projects=false \
  -F allow_auto_merge=true -F delete_branch_on_merge=true \
  -F allow_squash_merge=true -F allow_merge_commit=false -F allow_rebase_merge=false \
  -f squash_merge_commit_title=PR_TITLE -f squash_merge_commit_message=PR_BODY \
  -f description="SNES emulator that presents games in HD-2D — per-game profiles, AI-driven open source"
gh api -X PATCH "repos/$R" --silent --input - <<'EOF'
{"security_and_analysis": {"secret_scanning": {"status": "enabled"},
                           "secret_scanning_push_protection": {"status": "enabled"}}}
EOF
gh api -X PUT "repos/$R/topics" --silent --input - <<'EOF'
{"names": ["snes", "emulator", "hd-2d", "ares", "sdl3", "voxel", "ai-agents"]}
EOF

# Actions: read-only default token, workflows can't approve PRs,
# fork PR workflows wait for approval for every outside contributor.
gh api -X PUT "repos/$R/actions/permissions/workflow" --silent --input - <<'EOF'
{"default_workflow_permissions": "read", "can_approve_pull_request_reviews": false}
EOF
gh api -X PUT "repos/$R/actions/permissions/fork-pr-contributor-approval" --silent --input - <<'EOF'
{"approval_policy": "all_external_contributors"}
EOF

# Branch protection. app_id 15368 = GitHub Actions, so only the real workflow
# can satisfy `checks`. enforce_admins=false: the sole CODEOWNER can't approve
# his own PRs, so he merges protected-path work as admin.
gh api -X PUT "repos/$R/branches/main/protection" --silent --input - <<'EOF'
{
  "required_status_checks": {"strict": false, "checks": [{"context": "checks", "app_id": 15368}]},
  "enforce_admins": false,
  "required_pull_request_reviews": {
    "dismiss_stale_reviews": true,
    "require_code_owner_reviews": true,
    "require_last_push_approval": true,
    "required_approving_review_count": 1
  },
  "restrictions": null,
  "required_linear_history": true,
  "allow_force_pushes": false,
  "allow_deletions": false
}
EOF
echo "applied to $R; set the social preview and Discussion categories by hand"
