import os
import ssl
import stat
import tempfile
from pathlib import Path, PurePosixPath
from urllib.parse import quote, urlparse
from urllib.request import urlopen
from zipfile import BadZipFile, ZipFile

from helper.constants import MOD_SOURCE_ARCHIVE, MOD_SOURCE_DIRECTORY, MOD_SOURCE_FILES
from scanner.mod_catalog import ModDefinition, ModSourceFile


DOWNLOAD_TIMEOUT_SECONDS = 30
MAX_MOD_DOWNLOAD_SIZE_BYTES = 256 * 1024 * 1024
MAX_MOD_FILE_SIZE_BYTES = 256 * 1024 * 1024
MAX_MOD_TOTAL_SIZE_BYTES = 512 * 1024 * 1024
MAX_INI_SIZE_BYTES = 256 * 1024
MAX_README_SIZE_BYTES = 1024 * 1024
SYSTEM_CA_BUNDLE_PATHS = (
    Path("/etc/ssl/certs/ca-certificates.crt"),
    Path("/etc/ssl/cert.pem"),
)


def _create_ssl_context() -> ssl.SSLContext:
    for ca_bundle in SYSTEM_CA_BUNDLE_PATHS:
        if ca_bundle.is_file():
            return ssl.create_default_context(cafile=str(ca_bundle))
    return ssl.create_default_context()


def _repository_file_url(repository_url: str, repository_ref: str, path: str) -> str:
    repository = urlparse(repository_url)
    if repository.hostname != "github.com":
        raise ValueError("Mod source repository must be hosted on github.com")
    owner_and_repo = repository.path.strip("/").split("/")
    if len(owner_and_repo) != 2 or not all(owner_and_repo):
        raise ValueError("Mod source repository URL must identify an owner and repository")
    repository_path = "/".join(quote(part, safe="") for part in owner_and_repo)
    encoded_ref = quote(repository_ref, safe="")
    encoded_path = quote(path, safe="/")
    return (
        f"https://raw.githubusercontent.com/{repository_path}/"
        f"{encoded_ref}/{encoded_path}"
    )


def mod_readme_path(mod: ModDefinition) -> str:
    if mod.source.kind == MOD_SOURCE_FILES:
        source_path = mod.source.files[0].path
    elif mod.source.path is not None:
        source_path = mod.source.path
    else:
        raise ValueError(f"Mod {mod.mod_id} has no source path for its README")
    root = PurePosixPath(source_path).parts[0]
    return f"{root}/README.md"


def fetch_mod_readme(mod: ModDefinition) -> str:
    readme_url = _repository_file_url(
        mod.source.repository_url,
        mod.source.repository_ref,
        mod_readme_path(mod),
    )
    with urlopen(
        readme_url,
        timeout=DOWNLOAD_TIMEOUT_SECONDS,
        context=_create_ssl_context(),
    ) as response:
        content = response.read(MAX_README_SIZE_BYTES + 1)
    if len(content) > MAX_README_SIZE_BYTES:
        raise ValueError("Mod README exceeds the 1 MiB display limit")
    try:
        return content.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise ValueError("Mod README is not valid UTF-8 text") from error


def _target_path(game_root: Path, relative_path: str) -> Path:
    relative = PurePosixPath(relative_path)
    if (
        not relative_path
        or relative.is_absolute()
        or "\\" in relative_path
        or ":" in relative_path
        or any(part in ("", ".", "..") for part in relative_path.split("/"))
    ):
        raise ValueError(f"Unsafe mod target path: {relative_path}")

    target = game_root.joinpath(*relative.parts)
    if target.is_symlink():
        raise ValueError(f"Mod target is a symbolic link: {relative_path}")
    current = game_root
    for part in relative.parts[:-1]:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"Mod target traverses a symbolic link: {relative_path}")
    try:
        target.parent.resolve(strict=False).relative_to(game_root)
    except ValueError as error:
        raise ValueError(f"Mod target escapes the game directory: {relative_path}") from error
    return target


def _source_file_map(mod: ModDefinition) -> dict[str, str]:
    source = mod.source
    if source.kind == MOD_SOURCE_FILES:
        return {source_file.destination: source_file.path for source_file in source.files}
    if source.kind == MOD_SOURCE_DIRECTORY:
        return {path: f"{source.path}/{path}" for path in mod.files}
    if source.kind == MOD_SOURCE_ARCHIVE:
        prefix = f"{source.destination}/" if source.destination else ""
        result = {}
        for path in mod.files:
            if prefix and not path.startswith(prefix):
                raise ValueError(
                    f"Mod file {path} is outside archive destination {source.destination}"
                )
            result[path] = path[len(prefix) :] if prefix else path
        return result
    raise ValueError(f"Unsupported mod source kind: {source.kind}")


def _copy_limited(stream, target, max_bytes: int, description: str) -> int:
    total_bytes = 0
    while True:
        chunk = stream.read(min(1024 * 1024, max_bytes - total_bytes + 1))
        if not chunk:
            return total_bytes
        total_bytes += len(chunk)
        if total_bytes > max_bytes:
            raise ValueError(f"{description} exceeds the {max_bytes // (1024 * 1024)} MiB limit")
        target.write(chunk)


def _stage_stream(stream, target: Path, max_bytes: int) -> tuple[Path, int]:
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".anyaspectratio-",
        dir=target.parent,
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as staged:
            size = _copy_limited(stream, staged, max_bytes, "Mod file")
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise
    return temporary_path, size


def _files_equal(first: Path, second: Path) -> bool:
    if first.stat().st_size != second.stat().st_size:
        return False
    with first.open("rb") as left, second.open("rb") as right:
        while True:
            left_chunk = left.read(1024 * 1024)
            right_chunk = right.read(1024 * 1024)
            if left_chunk != right_chunk:
                return False
            if not left_chunk:
                return True


def apply_mod_files(
    mod: ModDefinition,
    game_path: str,
    *,
    replace_existing_files: bool | None = None,
) -> list[str]:
    game_root = Path(game_path).resolve(strict=True)
    if not game_root.is_dir():
        raise ValueError("Game installation directory is unavailable")

    source_files = _source_file_map(mod)
    targets = {
        relative_path: _target_path(game_root, relative_path)
        for relative_path in mod.files
    }
    staged_files: list[tuple[Path, Path]] = []
    try:
        if mod.source.kind == MOD_SOURCE_ARCHIVE:
            if mod.source.path is None:
                raise ValueError("Archive source path is missing")
            archive_url = _repository_file_url(
                mod.source.repository_url,
                mod.source.repository_ref,
                mod.source.path,
            )
            with urlopen(
                archive_url,
                timeout=DOWNLOAD_TIMEOUT_SECONDS,
                context=_create_ssl_context(),
            ) as response:
                archive_descriptor, archive_name = tempfile.mkstemp(
                    prefix="anyaspectratio-",
                    suffix=".zip",
                )
                archive_path = Path(archive_name)
                try:
                    with os.fdopen(archive_descriptor, "wb") as archive_file:
                        _copy_limited(
                            response,
                            archive_file,
                            MAX_MOD_DOWNLOAD_SIZE_BYTES,
                            "Mod archive download",
                        )
                    with ZipFile(archive_path) as archive:
                        members = {}
                        total_uncompressed_size = 0
                        for relative_path, member_name in source_files.items():
                            try:
                                info = archive.getinfo(member_name)
                            except KeyError as error:
                                raise ValueError(
                                    f"Mod archive is missing {member_name}"
                                ) from error
                            if info.is_dir():
                                raise ValueError(
                                    f"Mod archive entry is not a file: {member_name}"
                                )
                            if info.file_size > MAX_MOD_FILE_SIZE_BYTES:
                                raise ValueError(
                                    f"Mod file exceeds the "
                                    f"{MAX_MOD_FILE_SIZE_BYTES // (1024 * 1024)} MiB limit"
                                )
                            total_uncompressed_size += info.file_size
                            if total_uncompressed_size > MAX_MOD_TOTAL_SIZE_BYTES:
                                raise ValueError(
                                    f"Mod files exceed the "
                                    f"{MAX_MOD_TOTAL_SIZE_BYTES // (1024 * 1024)} MiB total limit"
                                )
                            members[relative_path] = info
                        for relative_path, info in members.items():
                            target = targets[relative_path]
                            with archive.open(info) as source_stream:
                                staged, actual_size = _stage_stream(
                                    source_stream,
                                    target,
                                    MAX_MOD_FILE_SIZE_BYTES,
                                )
                            staged_files.append((staged, target))
                            total_uncompressed_size += actual_size - info.file_size
                            if total_uncompressed_size > MAX_MOD_TOTAL_SIZE_BYTES:
                                raise ValueError(
                                    f"Mod files exceed the "
                                    f"{MAX_MOD_TOTAL_SIZE_BYTES // (1024 * 1024)} MiB total limit"
                                )
                except BadZipFile as error:
                    raise ValueError("Mod archive is invalid") from error
                finally:
                    archive_path.unlink(missing_ok=True)
        else:
            total_download_size = 0
            for relative_path, source_path in source_files.items():
                source_url = _repository_file_url(
                    mod.source.repository_url,
                    mod.source.repository_ref,
                    source_path,
                )
                with urlopen(
                    source_url,
                    timeout=DOWNLOAD_TIMEOUT_SECONDS,
                    context=_create_ssl_context(),
                ) as response:
                    staged, file_size = _stage_stream(
                        response,
                        targets[relative_path],
                        MAX_MOD_FILE_SIZE_BYTES,
                    )
                staged_files.append((staged, targets[relative_path]))
                total_download_size += file_size
                if total_download_size > MAX_MOD_TOTAL_SIZE_BYTES:
                    raise ValueError(
                        f"Mod files exceed the "
                        f"{MAX_MOD_TOTAL_SIZE_BYTES // (1024 * 1024)} MiB total limit"
                    )

        staged_by_target = {target: staged for staged, target in staged_files}
        conflicting_files = []
        for relative_path, target in targets.items():
            if target.is_dir():
                raise ValueError(f"Mod target is a directory: {relative_path}")
            staged = staged_by_target[target]
            if target.is_file() and not _files_equal(target, staged):
                conflicting_files.append(relative_path)
        if conflicting_files and replace_existing_files is None:
            return conflicting_files

        for relative_path, target in targets.items():
            staged = staged_by_target[target]
            if (
                replace_existing_files is False
                and relative_path in conflicting_files
            ):
                continue
            os.replace(staged, target)
        return []
    finally:
        for staged, _ in staged_files:
            staged.unlink(missing_ok=True)


def read_mod_ini(mod: ModDefinition, game_path: str) -> tuple[str, bool]:
    if mod.ini_file is None:
        raise ValueError("This mod does not define an INI configuration file")

    game_root = Path(game_path).resolve(strict=True)
    if not game_root.is_dir():
        raise ValueError("Game installation directory is unavailable")
    ini_path = _target_path(game_root, mod.ini_file)
    if not ini_path.exists():
        return "", False
    if not ini_path.is_file():
        raise ValueError("Mod INI path is not a regular file")
    with ini_path.open("rb") as ini_stream:
        content = ini_stream.read(MAX_INI_SIZE_BYTES + 1)
    if len(content) > MAX_INI_SIZE_BYTES:
        raise ValueError("Mod INI file exceeds the 256 KiB editor limit")
    try:
        return content.decode("utf-8"), True
    except UnicodeDecodeError as error:
        raise ValueError("Mod INI file is not valid UTF-8 text") from error


def write_mod_ini(mod: ModDefinition, game_path: str, content: str) -> None:
    if mod.ini_file is None:
        raise ValueError("This mod does not define an INI configuration file")
    if not isinstance(content, str):
        raise ValueError("Mod INI content must be text")

    encoded_content = content.encode("utf-8")
    if len(encoded_content) > MAX_INI_SIZE_BYTES:
        raise ValueError("Mod INI content exceeds the 256 KiB editor limit")

    game_root = Path(game_path).resolve(strict=True)
    if not game_root.is_dir():
        raise ValueError("Game installation directory is unavailable")
    ini_path = _target_path(game_root, mod.ini_file)
    if ini_path.exists() and not ini_path.is_file():
        raise ValueError("Mod INI path is not a regular file")

    ini_path.parent.mkdir(parents=True, exist_ok=True)
    existing_mode = (
        stat.S_IMODE(ini_path.stat().st_mode)
        if ini_path.exists()
        else 0o644
    )
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".anyaspectratio-ini-",
        dir=ini_path.parent,
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as target:
            target.write(encoded_content)
        os.chmod(temporary_path, existing_mode)
        os.replace(temporary_path, ini_path)
    finally:
        temporary_path.unlink(missing_ok=True)


def remove_mod_files(mod: ModDefinition, game_path: str) -> None:
    game_root = Path(game_path).resolve(strict=True)
    if not game_root.is_dir():
        raise ValueError("Game installation directory is unavailable")

    targets = [_target_path(game_root, path) for path in mod.files]
    for target in targets:
        target.unlink(missing_ok=True)

    for target in targets:
        parent = target.parent
        while parent != game_root:
            try:
                parent.rmdir()
            except OSError:
                break
            parent = parent.parent
