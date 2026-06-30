# Publishing OpenBrowser on Flathub

Flathub requires a manual pull request against the `new-pr` branch. Do not use AI tools to open or write the submission PR (see [Flathub requirements](https://docs.flathub.org/docs/for-app-authors/requirements)).

## 1. Push upstream changes

Commit and push the Flatpak metadata, app ID updates, and tag `v0.1`:

```bash
git add .
git commit -m "Add Flatpak packaging for Flathub"
git tag v0.1
git push origin main
git push origin v0.1
```

Update the `commit` field in `flatpak/io.github.ramm_fr.OpenBrowser.yml` if the tag points to a different commit.

## 2. Test the build locally

```bash
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install flathub org.flatpak.Builder org.gnome.Platform//48 org.gnome.Sdk//48

cd flatpak
flatpak run org.flatpak.Builder --force-clean --install --user build-dir io.github.ramm_fr.OpenBrowser.yml
flatpak run --branch=stable --command=openbrowser io.github.ramm_fr.OpenBrowser
```

Or use the Flathub helper:

```bash
flatpak run org.flatpak.Builder --force-clean build-dir io.github.ramm_fr.OpenBrowser.yml
flatpak run --command=flathub-build org.flatpak.Builder --install flatpak/io.github.ramm_fr.OpenBrowser.yml
```

## 3. Submit to Flathub

```bash
git clone --branch=new-pr https://github.com/YOUR_GITHUB_USERNAME/flathub.git
cd flathub
git checkout -b add-openbrowser new-pr

cp /path/to/OpenBrowser/flatpak/io.github.ramm_fr.OpenBrowser.yml .
git add io.github.ramm_fr.OpenBrowser.yml
git commit -m "Add io.github.ramm_fr.OpenBrowser"
git push -u origin add-openbrowser
```

Open a pull request on GitHub:

- **Base branch:** `new-pr` (not `master`)
- **Title:** `Add io.github.ramm_fr.OpenBrowser`

## 4. After approval

Flathub creates `flathub/io.github.ramm_fr.OpenBrowser`. Future releases are published by updating the manifest tag/commit in that repository.

Enable 2FA on GitHub before submitting; you must accept the maintainer invite within one week.
