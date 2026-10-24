#!/bin/sh
# Starts the game from its own folder, where it looks for its assets.
cd "$(dirname "$(readlink -f "$0")")" || exit 1
exec ./1080SnowboardingRecompiled "$@"
