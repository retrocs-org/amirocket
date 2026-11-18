# Refresh scripts for the web server

Copy `refresh-amirocket.sh` and `refresh-amihass.sh` to `/srv/www/vhosts` on
sth-ts-web003, make them executable, then run them as root. Each script clones
its public GitHub repository's `main` branch. It validates PHP before replacing
its corresponding document root, and applies ownership `wwwrun:wwwrun`, directory
mode 750 and file mode 640. No Apache reload is needed for a content refresh.

Requirements: git, PHP CLI 8.1+, getent and standard Linux utilities. AmiRocket
also requires Python 3.9+ to prepare its website and copy documentation. AmiHASS
serves its existing public-web directory directly. PHP's web-worker configuration
still needs HTTPS access to GitHub for release metadata; CLI lint does not test
that connection or certify all runtime behavior.

The entire document root is managed by the refresh script: keep uploads, private
configuration and hand-maintained files outside it. The document root must be a
normal directory, not a symlink or mount point. Staging is on the same filesystem.
The old directory is retained until the new tree is ready, with restoration if
installing the new tree fails. There is a brief gap between the two renames.
Run only one refresh per site at a time; the scripts are intended for this simple
Apache layout, not concurrent deployment or a persistent rollback history.

Only committed and pushed changes are deployed. Local changes do not appear on
the server until pushed to GitHub. These scripts do not push source or publish
application releases.
