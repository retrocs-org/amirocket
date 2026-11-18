#!/bin/sh
# Refresh the public PHP website from GitHub main.
set -eu
APP="amihass"
PARENT=/srv/www/vhosts
SITE="$PARENT/$APP.retrocs.org"
REPOSITORY="https://github.com/retrocs-org/$APP.git"

[ "$(id -u)" -eq 0 ] || { echo "Run as root." >&2; exit 1; }
for program in git php ; do
    command -v "$program" >/dev/null 2>&1 || { echo "Missing command: $program" >&2; exit 1; }
done
[ ! -L "$SITE" ] || { echo "Refusing a symlink document root." >&2; exit 1; }
[ ! -e "$SITE" ] || [ -d "$SITE" ] || { echo "Document root is not a directory." >&2; exit 1; }
getent passwd wwwrun >/dev/null
getent group wwwrun >/dev/null
mkdir -p "$PARENT"
TEMP=$(mktemp -d "$PARENT/.refresh-$APP.XXXXXX")
cleanup() {
    result=$?
    trap - 0 HUP INT TERM
    if [ -d "$TEMP/previous" ] && [ ! -e "$SITE" ]; then
        if ! mv "$TEMP/previous" "$SITE"; then
            echo "Restore failed. Previous site retained at $TEMP/previous" >&2
            exit 1
        fi
    fi
    rm -rf -- "$TEMP"
    exit "$result"
}
trap cleanup 0
trap 'exit 1' HUP INT TERM

git clone --quiet --depth 1 --single-branch --branch main     "$REPOSITORY" "$TEMP/repository"
SOURCE="$TEMP/repository/public-web"
[ -f "$SOURCE/index.php" ] || {
    echo "Website index.php is missing; site left unchanged." >&2
    exit 1
}
# Lint all PHP before touching the live document root.
find "$SOURCE" -type f -name '*.php' -exec sh -c '
    for file do php -l "$file" >/dev/null || exit 1; done
' sh {} +
mkdir "$TEMP/new"
cp -R "$SOURCE/." "$TEMP/new/"
chown -R wwwrun:wwwrun "$TEMP/new"
find "$TEMP/new" -type d -exec chmod 750 {} +
find "$TEMP/new" -type f -exec chmod 640 {} +

# Stage on the same filesystem; keep the old site until the new tree is ready.
[ ! -L "$SITE" ] || { echo "Document root became a symlink; refusing update." >&2; exit 1; }
if [ -e "$SITE" ]; then mv "$SITE" "$TEMP/previous"; fi
mv "$TEMP/new" "$SITE"
echo "Updated $SITE from GitHub main."
