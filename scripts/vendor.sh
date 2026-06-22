#!/usr/bin/env sh
# Add the Sunshine + Moonlight forks as git submodules under vendor/.
# Fork upstream on GitHub FIRST, then point these at YOUR forks.
set -eu

SUNSHINE_FORK="${SUNSHINE_FORK:-https://github.com/TheStonedGamer/Sunshine.git}"
MOONLIGHT_FORK="${MOONLIGHT_FORK:-https://github.com/TheStonedGamer/moonlight-qt.git}"

cd "$(dirname "$0")/.."

if [ ! -d vendor/sunshine/.git ] && [ ! -f vendor/sunshine/.git ]; then
  echo ">> adding vendor/sunshine  <- $SUNSHINE_FORK"
  git submodule add "$SUNSHINE_FORK" vendor/sunshine
fi
if [ ! -d vendor/moonlight/.git ] && [ ! -f vendor/moonlight/.git ]; then
  echo ">> adding vendor/moonlight <- $MOONLIGHT_FORK"
  git submodule add "$MOONLIGHT_FORK" vendor/moonlight
fi

echo ">> initializing submodules recursively (pulls moonlight-common-c)"
git submodule update --init --recursive

cat <<'EOF'

Done. Next:
  - Pin commits:  git -C vendor/sunshine checkout <sha> && git add vendor/sunshine
                  git -C vendor/moonlight checkout <sha> && git add vendor/moonlight
  - Commit the submodule pointers.
EOF
