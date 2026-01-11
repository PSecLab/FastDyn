#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 NAME"
  exit 1
fi

NAME="$1"

# Create directories
mkdir -p \
  "$NAME/labeled_conf" \
  "$NAME/unlabeled_conf"

# Create files
touch \
  "$NAME/labeled_conf/modifiers.txt" \
  "$NAME/labeled_conf/virtuals.txt" \
  "$NAME/unlabeled_conf/modifiers.txt" \
  "$NAME/unlabeled_conf/virtuals.txt"

cp labeled_conf/modifiers.txt "$NAME/labeled_conf/modifiers.txt"
cp labeled_conf/virtuals.txt "$NAME/labeled_conf/virtuals.txt"

echo "Created configuration tree for '$NAME'"
