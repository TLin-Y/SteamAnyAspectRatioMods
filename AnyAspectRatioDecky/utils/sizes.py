"""Byte size units for limits, and a short label for messages."""

KIB = 1024
MIB = 1024 * KIB
GIB = 1024 * MIB


def label(size_bytes: int) -> str:
    """The size in its largest whole unit, rounded down: 256 * KIB is "256 KiB", 2 * GIB is "2 GiB"."""
    if size_bytes >= GIB:
        return f"{size_bytes // GIB} GiB"
    if size_bytes >= MIB:
        return f"{size_bytes // MIB} MiB"
    return f"{size_bytes // KIB} KiB"
