#!/bin/sh
set -eu

bundle_path="${1:-}"
if [ -z "$bundle_path" ]; then
    echo "CodeSignMacBundleAfterPostBuild.sh requires a bundle path." >&2
    exit 2
fi

if [ "${CODE_SIGNING_ALLOWED:-YES}" = "NO" ]; then
    exit 0
fi

identity="${EXPANDED_CODE_SIGN_IDENTITY:-}"
if [ -z "$identity" ]; then
    identity="${CODE_SIGN_IDENTITY:--}"
fi
if [ -z "$identity" ]; then
    identity="-"
fi

entitlements_path=""
candidate_entitlements="${TARGET_TEMP_DIR:-}/${FULL_PRODUCT_NAME:-}.xcent"
if [ -n "${TARGET_TEMP_DIR:-}" ] && [ -n "${FULL_PRODUCT_NAME:-}" ] && [ -f "$candidate_entitlements" ]; then
    entitlements_path="$candidate_entitlements"
fi

if [ -n "$entitlements_path" ]; then
    /usr/bin/codesign --force --strict --sign "$identity" --entitlements "$entitlements_path" "$bundle_path"
else
    /usr/bin/codesign --force --strict --sign "$identity" "$bundle_path"
fi
