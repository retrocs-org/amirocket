# AmiRocket website

A responsive PHP website. No Composer packages, external fonts or tracking.
PHP 8.1+ is required to host it; preparation also needs Python 3.9+.
The chat and Preferences images are real native emulator screenshots with sample conversations. See SCREENSHOTS.md for provenance and reproduction steps.

Website changes are recorded separately in [changelog.md](changelog.md).
The application changelog remains in the repository root; preparation copies it
to `dist/docs/changelog.md` and the website changelog to `dist/changelog.md`.

## Prepare and preview

```sh
python3 public-web/prepare.py
php -S 127.0.0.1:8080 -t public-web/dist
```

Open http://127.0.0.1:8080. Preparation copies page assets, release helpers and
current documentation, renders the PHP offline, and checks page links. It does
not require a local application build or release archives. Serve `dist` only.
Upload its contents to a PHP-enabled host with `index.php` as the directory index.
No special routing is needed. The website has not been deployed.

## GitHub releases

`includes/releases.php` adapts NovaDock’s release catalogue. The repository is
`https://github.com/retrocs-org/amirocket`; change `AR_RELEASE_REPO` and
`AR_RELEASE_API` together if the repository moves. No API token is used.

Web requests fetch public releases with a four-second timeout and a 2 MiB response
limit, TLS verification and no redirects. Metadata is cached outside the web root
in the system temporary directory for 15 minutes, with ETag revalidation,
non-blocking refresh locks and a one-minute retry interval after failures.
A previous successful catalogue remains available during outages. PHP needs cURL
or HTTPS URL streams plus a writable system temporary directory for refreshes.

Only non-draft releases with valid version tags/asset names are displayed.
Download URLs must exactly match fully uploaded, non-empty assets in this GitHub repository. Runtime LHA,
SDK ZIP, source ZIP and runtime checksum links come from the API; absent assets
stay unavailable. Published prereleases are labelled as previews. Runtime, SDK and source download counts and file sizes come from each GitHub asset and refresh every 15 minutes. Counts measure asset downloads, not unique users or installations. Missing counts are omitted rather than invented; a reported zero is shown as zero. No download
URLs are guessed, and no local application archives are served.

`releases.json` is an empty optional offline fallback until a release is published.
Do not populate it with planned/unpublished assets. CLI rendering stays offline;
web requests refresh automatically. With no published release, the page shows
“First release coming soon”. During a fetch failure it also explains that GitHub
could not be checked. Release versions and links are independent of root VERSION.

Run `php public-web/tests/releases.php` to check normalization, asset validation,
draft rejection, caching, outages and the no-release state without network access.

Edit `index.php`, `styles.css`, `app.js` or the helper, then prepare again.
Generated `dist` is ignored and excluded from application source archives.
Website code is GPL-3.0-or-later. NovaDock-derived release code retains the MIT
notice in `licenses/NovaDock.txt`. The icon is the existing application icon.
