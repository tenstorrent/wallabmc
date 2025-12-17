#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# Get the git SHA1 of the project with optional -dirty suffix
# Usage: get_project_git_sha.sh [project_directory]
# Outputs: git SHA1 (short) with -dirty suffix if working directory has uncommitted changes

if [ -n "$CI_COMMIT_SHA" ]; then
	echo "${CI_COMMIT_SHA:0:7}"
	exit 0
fi

PROJECT_DIR="${1:-${CI_PROJECT_DIR:-.}}"

PROJECT_GIT_SHA=$(git -C "$PROJECT_DIR" rev-parse --short HEAD 2>/dev/null) || exit 1
# Check if working directory is dirty and append -dirty tag if so
if [ "$PROJECT_GIT_SHA" != "unknown" ]; then
	if ! git -C "$PROJECT_DIR" diff --quiet 2>/dev/null; then
		PROJECT_GIT_SHA="${PROJECT_GIT_SHA}-dirty"
	fi
fi

echo "$PROJECT_GIT_SHA"
