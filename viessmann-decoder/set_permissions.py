#!/usr/bin/env python3
"""
Script to set proper file permissions for Viessmann Addon
This works around filesystem access issues in the dev container
"""

import os
import stat
from pathlib import Path

def set_executable(file_path):
    """Set executable permissions on a file"""
    try:
        # Get current permissions
        current = os.stat(file_path).st_mode
        # Add executable permissions for owner, group, and others
        new_perms = current | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
        # Set the new permissions
        os.chmod(file_path, new_perms)
        print(f"✅ Set executable permissions on {file_path}")
        return True
    except Exception as e:
        print(f"❌ Failed to set permissions on {file_path}: {e}")
        return False

def main():
    """Set executable permissions on all required files"""
    addon_dir = Path("/workspaces/developers.home-assistant/Viessmann-Home-Assistant-Addon-/viessmann-decoder")
    
    # List of files that need executable permissions
    executable_files = [
        addon_dir / "startup.sh",
        addon_dir / "build_enhanced.sh", 
        addon_dir / "test_addon.sh",
        addon_dir / "validate_complete.sh",
        addon_dir / "rootfs/etc/s6-overlay/s6-rc.d/viessmann-decoder/run"
    ]
    
    print("🔧 Setting executable permissions for Viessmann Addon files...")
    
    success_count = 0
    for file_path in executable_files:
        if file_path.exists():
            if set_executable(file_path):
                success_count += 1
        else:
            print(f"⚠️  File not found: {file_path}")
    
    print(f"\n📊 Summary: {success_count}/{len(executable_files)} files updated successfully")
    
    if success_count == len([f for f in executable_files if f.exists()]):
        print("✅ All file permissions set correctly!")
        return True
    else:
        print("❌ Some files failed to update permissions")
        return False

if __name__ == "__main__":
    success = main()
    exit(0 if success else 1)