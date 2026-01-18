# Solution Summary: Fixing "[FATAL tini (7)] exec /init failed: Permission denied"

## Problem Statement
The Viessmann Decoder Home Assistant addon was failing to start with the error:
```
[FATAL tini (7)] exec /init failed: Permission denied
```

## Root Cause Analysis

### Background: S6-Overlay v3
Home Assistant's base Docker images use S6-Overlay v3, a process supervisor that must run as PID 1 (the first process in the container). S6-Overlay manages services defined in `/etc/services.d/` directories.

### The Conflict
When `init: false` is **not** set in the addon's `config.yaml`:
1. The Home Assistant Supervisor injects Docker's default init system (tini)
2. Tini attempts to run as PID 1
3. Tini tries to execute the `/init` script (S6-Overlay's entrypoint)
4. This creates a conflict: two init systems competing for PID 1
5. Result: Permission denied error and addon failure

## The Solution

### Simple Fix
Add one line to `config.yaml`:
```yaml
init: false
```

### What This Does
- Tells the Home Assistant Supervisor **not** to inject Docker's tini init system
- Allows S6-Overlay's `/init` to run directly as PID 1
- S6-Overlay can then properly manage the addon's services

### Complete Configuration
```yaml
name: Viessmann Decoder
version: 2.1.1
slug: viessmann-decoder
# ... other settings ...
hassio_api: true
hassio_role: default
init: false  # ← The critical fix
options:
  serial_port: /dev/ttyUSB0
  # ... other options ...
```

## Implementation Details

### Files Changed
1. **viessmann-decoder/config.yaml**
   - Added `init: false`
   - Bumped version to 2.1.1

2. **viessmann-decoder/CHANGELOG.md**
   - Documented the fix
   - Explained technical details

3. **viessmann-decoder/README.md**
   - Updated troubleshooting section
   - Added clear explanation of the issue

### Service Structure
The addon uses proper S6-Overlay v3 directory structure:
```
rootfs/
└── etc/
    └── services.d/
        └── viessmann-decoder/
            └── run  (executable, 755 permissions)
```

### Dockerfile Structure
The Dockerfile:
- Does NOT include `CMD` or `ENTRYPOINT`
- Relies on the base image's `/init` (from S6-Overlay)
- Copies the rootfs structure with service definitions

## Verification

### Automated Validation
Created `.validation/verify_s6_config.sh` that checks:
1. ✓ `init: false` is present in config.yaml
2. ✓ No CMD or ENTRYPOINT in Dockerfile
3. ✓ Service script has executable permissions
4. ✓ Git tracks executable flag properly
5. ✓ Service uses bashio shebang

### Running Validation
```bash
cd .validation
./verify_s6_config.sh
```

All checks pass ✓

## Why This Works

### Official Home Assistant Standard
This configuration matches **all** official Home Assistant addons:
- mosquitto (MQTT broker)
- mariadb (Database)
- example addon (Template)

### S6-Overlay v3 Requirements
According to [Home Assistant's S6-Overlay v3 migration guide](https://developers.home-assistant.io/blog/2022/05/12/s6-overlay-base-images/):
- S6-Overlay v3 requires `init: false` to function correctly
- This is the standard configuration for modern Home Assistant addons
- Without it, init system conflicts occur

## Best Practices Learned

### For Home Assistant Addon Development
1. Always set `init: false` in config.yaml when using base images
2. Use S6-Overlay service structure (`/etc/services.d/`)
3. Ensure service scripts are executable (chmod +x, git mode 100755)
4. Don't override CMD/ENTRYPOINT in Dockerfile
5. Use bashio for proper Home Assistant integration

### For This Addon Specifically
- Service script: `/etc/services.d/viessmann-decoder/run`
- Uses bashio for configuration management
- Properly integrated with S6-Overlay v3
- Follows official addon conventions

## Testing Recommendations

### For Users
1. Update to version 2.1.1 or later
2. Restart the addon
3. The error should be resolved

### For Developers
1. Run validation script before committing changes
2. Test addon in actual Home Assistant environment
3. Verify service starts correctly with `docker logs`
4. Check that S6-Overlay manages the service (ps aux should show s6 processes)

## References

- [Home Assistant S6-Overlay v3 Migration Guide](https://developers.home-assistant.io/blog/2022/05/12/s6-overlay-base-images/)
- [Add-on Configuration Documentation](https://developers.home-assistant.io/docs/add-ons/configuration/)
- [Official Addon Examples](https://github.com/home-assistant/addons-example)
- [Mosquitto Addon Config](https://github.com/home-assistant/addons/blob/master/mosquitto/config.yaml)

## Conclusion

The fix is simple but critical: adding `init: false` to config.yaml prevents Docker's tini from conflicting with S6-Overlay v3, allowing the addon to start correctly. This is the standard configuration for all modern Home Assistant addons and should always be included when using Home Assistant's base images.
