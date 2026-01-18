# Build Verification Guide

This document describes the Docker permission improvements made to fix `/init` permission denied errors and how to verify the build.

## Changes Made

### 1. Enhanced Dockerfile Permissions (v2.1.2)

**Location**: `viessmann-decoder/Dockerfile` (lines 50-57)

**Change**: Improved permission handling for S6-Overlay v3 service scripts

**Before**:
```dockerfile
COPY rootfs/ /
RUN chmod +x /etc/services.d/viessmann-decoder/run
```

**After**:
```dockerfile
COPY rootfs/ /
# Ensure all service scripts have proper executable permissions
# Using chmod during build ensures permissions are correctly set regardless of host filesystem
# This is critical for S6-Overlay v3 to properly manage services
RUN chmod -R +x /etc/services.d/*/
```

**Benefits**:
- More robust: Works regardless of host filesystem state
- Future-proof: Automatically handles additional services
- Comprehensive: Applies to all scripts in services.d directories
- Best practice: Follows Docker build recommendations (2024/2025)

### 2. Verified Existing Configuration

**Location**: `viessmann-decoder/config.yaml` (line 33)

**Verified**: `init: false` is properly set

This critical setting prevents Docker's tini init system from conflicting with S6-Overlay v3.

## Verification Methods

### Method 1: Dockerfile Linting

```bash
cd viessmann-decoder
docker run --rm -i hadolint/hadolint < Dockerfile
```

**Expected Result**: No errors (only optional warnings about package version pinning)

### Method 2: HA_builder Test Build

Using the official Home Assistant builder tool:

```bash
# Pull the builder
docker pull ghcr.io/home-assistant/amd64-builder:latest

# Run test build (replace 'testuser' with your Docker Hub username)
docker run --rm --privileged \
  -v /var/run/docker.sock:/var/run/docker.sock:ro \
  -v $(pwd)/viessmann-decoder:/data \
  ghcr.io/home-assistant/amd64-builder:latest \
  --test \
  --amd64 \
  --target /data \
  --docker-hub testuser \
  --image viessmann-decoder
```

**Expected Result**: Build completes successfully and creates a Docker image

### Method 3: Manual Docker Build

```bash
cd viessmann-decoder
docker build \
  --build-arg BUILD_FROM=ghcr.io/home-assistant/amd64-base:3.18 \
  --build-arg BUILD_ARCH=amd64 \
  --build-arg BUILD_VERSION=2.1.2 \
  -t local/viessmann-decoder:test \
  .
```

**Expected Result**: Build completes and image is created

### Method 4: Verify Permissions in Built Image

After building, check that service scripts have proper permissions:

```bash
# Inspect the built image
docker run --rm local/viessmann-decoder:test ls -la /etc/services.d/viessmann-decoder/

# Expected output should show:
# -rwxr-xr-x (755 permissions) for the 'run' script
```

## Key Technical Details

### Why These Changes Matter

1. **Permission Issues**: Different host filesystems and git configurations may not preserve executable bits
2. **S6-Overlay v3**: Requires all service scripts to be executable (755 permissions)
3. **Build Reliability**: Setting permissions during Docker build ensures consistency across all environments
4. **Docker Best Practice**: Modern approach using explicit chmod during build (Docker 20.10+)

### Alternative Approaches Considered

1. **COPY --chmod**: Would require Docker 20.10+ and applies to all copied files
   - Not ideal for directory structures with mixed permissions
   
2. **Git filemode**: Relies on git preserving executable bit
   - Not reliable across different OS and git configurations
   
3. **Specific file chmod**: Original approach, less maintainable
   - Requires updating Dockerfile for each new service

### Current Solution Benefits

Our chosen approach (`chmod -R +x /etc/services.d/*/`) provides:
- ✓ Compatibility with all Docker versions
- ✓ Automatic handling of future services
- ✓ Independence from host filesystem state
- ✓ Explicit and clear intent
- ✓ Minimal image layers

## Integration with CI/CD

### GitHub Actions Example

```yaml
name: Build and Test

on: [push, pull_request]

jobs:
  build:
    name: Test build
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4
        
      - name: Test build with HA Builder
        uses: MrTir1995/HA_builder@main
        with:
          args: |
            --test \
            --amd64 \
            --target viessmann-decoder \
            --docker-hub testuser \
            --image viessmann-decoder
```

## Common Issues and Solutions

### Issue: "Permission denied" during build

**Symptom**: Build fails with permission errors

**Solution**: Ensure Docker has proper permissions and the build is run with `--privileged` flag

### Issue: Network timeout during package installation

**Symptom**: `apk add` fails with timeout or permission denied

**Solution**: Check network connectivity and Docker network settings

### Issue: S6-Overlay fails to start service

**Symptom**: Container starts but service doesn't run

**Solution**: 
1. Verify `init: false` in config.yaml
2. Check service script has shebang: `#!/usr/bin/with-contenv bashio`
3. Confirm permissions with: `docker run --rm <image> ls -la /etc/services.d/*/run`

## References

- [Home Assistant S6-Overlay v3 Migration Guide](https://developers.home-assistant.io/blog/2022/05/12/s6-overlay-base-images/)
- [Home Assistant Add-on Configuration](https://developers.home-assistant.io/docs/add-ons/configuration/)
- [Docker COPY --chmod Documentation](https://docs.docker.com/reference/dockerfile/#copy---chmod)
- [HA_builder Repository](https://github.com/MrTir1995/HA_builder)

## Version History

- **v2.1.2** (2026-01-18): Enhanced Docker build permissions with `chmod -R +x /etc/services.d/*/`
- **v2.1.1** (2026-01-18): Initial fix with `init: false` and basic permissions
- **v2.1.0** (2026-01-17): Initial release
