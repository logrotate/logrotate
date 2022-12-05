#!/bin/bash
set -x
SELF="$0"
TAG="$1"
TOKEN="$2"

NAME="logrotate"
NV="${NAME}-${TAG}"

usage() {
    printf "Usage: %s TAG TOKEN\n" "$SELF" >&2
    exit 1
}

die() {
    printf "%s: error: %s\n" "$SELF" "$*" >&2
    exit 1
}

# check arguments
test "$TAG" = "$(git describe --tags "$TAG")" || usage
test -n "$TOKEN" || usage

# check that .tar.gz is prepared
TAR_GZ="${NV}.tar.gz"
test -e "$TAR_GZ" || die "$TAR_GZ does not exist"

# check that .tar.xz is prepared
TAR_XZ="${NV}.tar.xz"
test -e "$TAR_XZ" || die "$TAR_XZ does not exist"

# sign the tarballs
for file in "$TAR_GZ" "$TAR_XZ"; do
    gpg --armor --detach-sign "$file" || die "tarball signing failed"
    test -f "${file}.asc" || die "tarball signature was not created"
done

# file to store response from GitHub API
JSON="./${NV}-github-release.js"

# create a new release on GitHub
curl "https://api.github.com/repos/${NAME}/${NAME}/releases" \
    -o "$JSON" --fail --verbose \
    --header "Authorization: token $TOKEN" \
    --data '{
    "tag_name": "'"$TAG"'",
    "target_commitish": "master",
    "name": "'"$NV"'",
    "draft": false,
    "prerelease": false
}' || exit $?

# parse upload URL from the response
UPLOAD_URL="$(grep '^ *"upload_url": "' "$JSON" \
    | sed -e 's/^ *"upload_url": "//' -e 's/{.*}.*$//')"
grep '^https://uploads.github.com/.*/assets$' <<< "$UPLOAD_URL" || exit $?

# upload both .tar.gz and .tar.xz
for comp in gzip xz; do
    file="${NV}.tar.${comp:0:2}"
    curl "${UPLOAD_URL}?name=${file}" \
        -T "$file" --fail --verbose \
        --header "Authorization: token $TOKEN" \
        --header "Content-Type: application/x-${comp}" \
        || exit $?
done

# upload signatures
for file in "${TAR_GZ}.asc" "${TAR_XZ}.asc"; do
    curl "${UPLOAD_URL}?name=${file}" \
        -T "$file" --fail --verbose \
        --header "Authorization: token $TOKEN" \
        --header "Content-Type: text/plain" \
        || exit $?
done
