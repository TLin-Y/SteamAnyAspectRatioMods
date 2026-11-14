"""Build flavor switches.

The GitHub release build keeps the in-plugin updater. The Decky Plugin Store
build (scripts/export_store_repo.py) rewrites this to False, because store
users receive updates through the store instead.
"""

SELF_UPDATE_ENABLED = True
