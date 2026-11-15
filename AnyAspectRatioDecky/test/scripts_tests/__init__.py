import sys

from support.sandbox import PLUGIN_ROOT

# The scripts are run directly (python scripts/x.py), so they import each other from their own folder.
sys.path.insert(0, str(PLUGIN_ROOT / "scripts"))
