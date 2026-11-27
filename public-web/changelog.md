# AmiRocket website changelog

Website changes are tracked separately from the application changelog in
the repository root `CHANGELOG.md`.

## Unreleased

### Features

- Add a responsive PHP landing page with a dark design.
- Introduce Rocket.Chat and link to its official website.
- Show native Amiga emulator screenshots of the client and Preferences.
- Fetch published release versions, download links, asset sizes and download
  counts from the AmiRocket GitHub repository.
- Provide links to the application manual, release guide, source and plugin SDK.

### Enhancements

- Cache GitHub release information for 15 minutes and retain saved information
  during temporary outages.
- Clearly show when no release or downloadable asset is available.
- Keep website changes in a dedicated changelog included in the prepared site.

### Bugs

- Validate release assets and page links during website preparation.
