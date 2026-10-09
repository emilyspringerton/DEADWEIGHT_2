#!/usr/bin/env bash
# build-image.sh [TAG] — Cloud Build the dw2_server image from tracked source (hermetic, unlike
# DEADWEIGHT's own binary-staging stopgap — matches SHANKPIT's own ops/docker/*.Dockerfile
# convention: build-from-source-in-Cloud-Build). See EMILY/gitops/CI_SETUP.md for the one-time
# GCP setup (Workload Identity) this depends on.
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:-$(git -C "$SRC" rev-parse --short HEAD)}"
PROJECT="${PROJECT:-project-d24a71e9-2daf-4b2d-917}"
CTX="$(mktemp -d)"; trap 'rm -rf "$CTX"' EXIT
git -C "$SRC" ls-files -z -- core apps/server cannon | (cd "$SRC" && xargs -0 -r tar cf - 2>/dev/null) | tar xf - -C "$CTX"
cp "$SRC/ops/docker/dw2.Dockerfile" "$CTX/Dockerfile"

BUILD_ID=$(gcloud builds submit "$CTX" --project "$PROJECT" \
  --tag "us-central1-docker.pkg.dev/$PROJECT/emily/deadweight2:$TAG" \
  --gcs-source-staging-dir="gs://${PROJECT}_cloudbuild/source" \
  --async --format="value(id)")

echo "submitted build $BUILD_ID, polling for completion..."
while true; do
  STATUS=$(gcloud builds describe "$BUILD_ID" --project "$PROJECT" --format="value(status)")
  case "$STATUS" in
    SUCCESS) echo "deadweight2:$TAG"; exit 0 ;;
    FAILURE|INTERNAL_ERROR|TIMEOUT|CANCELLED|EXPIRED) echo "build $BUILD_ID: $STATUS" >&2; exit 1 ;;
    *) sleep 5 ;;
  esac
done
