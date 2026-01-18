#!/bin/bash
set -e

echo "DEBUG: Starting custom debug entrypoint"
echo "DEBUG: Current User: $(id)"
echo "DEBUG: Listing root directory contents:"
ls -la /
echo "DEBUG: Detailed stats for /init:"
stat /init || echo "stat failed"
ls -l /init

echo "DEBUG: Checking attribute flags (if installed)"
lsattr /init 2>/dev/null || echo "lsattr not available"

echo "DEBUG: Attempting to force execution permissions on /init"
chmod -v 755 /init || echo "chmod failed"

echo "DEBUG: Checking /init dependencies (ldd)"
ldd /init || echo "ldd failed"

echo "DEBUG: Mounting info"
mount | grep " / "

echo "DEBUG: Executing /init now..."
exec /init "$@"
