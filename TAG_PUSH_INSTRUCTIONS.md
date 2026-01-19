# Tag Push Instructions for v2.1.5

## Current Status

✅ Git tag `v2.1.5` has been created locally
✅ CHANGELOG.md has been updated to document version 2.1.5
✅ All version files are correctly set to 2.1.5:
   - `viessmann-decoder/config.yaml`
   - `viessmann-decoder/build.json`
   - `viessmann-decoder/Dockerfile`

## Manual Step Required

Due to environment limitations, the tag needs to be pushed manually. After merging this PR, please run:

```bash
# Checkout the main branch (or the branch where the tag should be)
git checkout main

# Pull the latest changes
git pull origin main

# Create the tag (if not already present)
git tag -a v2.1.5 -m "Release version 2.1.5"

# Push the tag to origin
git push origin v2.1.5
```

## Verify Tag

After pushing, verify the tag exists:

```bash
git ls-remote --tags origin v2.1.5
```

## Next Steps

After pushing the tag:
1. Create a GitHub Release at: https://github.com/MrTir1995/Viessmann-HA-Addon/releases
2. Select tag `v2.1.5`
3. Title: `Version 2.1.5`
4. Copy release notes from CHANGELOG.md
5. Publish the release

This will trigger the GitHub Actions workflow to build and publish the Docker images to GHCR.

## Note

This file can be deleted after the tag has been successfully pushed.
