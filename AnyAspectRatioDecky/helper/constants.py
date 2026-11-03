import re


STEAM_ROOT_ENVIRONMENT_VARIABLE = "STEAM_COMPAT_CLIENT_INSTALL_PATH"
STEAM_ROOT_RELATIVE_PATHS = (
    ".local/share/Steam",
    ".steam/steam",
    "Steam",
)
STEAMAPPS_DIRECTORY = "steamapps"
STEAM_COMMON_DIRECTORY = "common"
STEAM_LIBRARY_CONFIG_FILE = "libraryfolders.vdf"
STEAM_LIBRARY_FOLDERS_KEY = "libraryfolders"
STEAM_APP_MANIFEST_GLOB = "appmanifest_*.acf"
STEAM_APP_MANIFEST_FILENAME_FORMAT = "appmanifest_{app_id}.acf"
STEAM_APP_MANIFEST_PATTERN = re.compile(r"appmanifest_(\d+)\.acf\Z")

VDF_TOKEN_PATTERN = re.compile(r'"((?:\\.|[^"\\])*)"|(\{|\})|//[^\r\n]*|([^\s{}"]+)')
VDF_UNEXPECTED_CHARACTER_MESSAGE = "Unexpected character at offset {offset}"
VDF_INVALID_ESCAPE_MESSAGE = "Invalid escape in quoted VDF value"
VDF_UNEXPECTED_CLOSING_BRACE_MESSAGE = "Unexpected closing brace"
VDF_MISSING_OBJECT_KEY_MESSAGE = "Object key is missing"
VDF_MISSING_VALUE_MESSAGE = "Value is missing"
VDF_UNCLOSED_OBJECT_MESSAGE = "Unclosed object"
VDF_TRAILING_DATA_MESSAGE = "Unexpected trailing VDF data"

VDF_PATH_KEY = "path"
STEAM_APP_STATE_KEY = "AppState"
STEAM_APP_ID_KEY = "appid"
STEAM_APP_NAME_KEY = "name"
STEAM_APP_INSTALL_DIRECTORY_KEY = "installdir"
STEAM_VDF_ENCODING = "utf-8-sig"

STEAM_LIBRARY_CONFIG_NOT_FOUND_WARNING = "Library configuration not found: {path}"
STEAM_LIBRARY_CONFIG_READ_WARNING = "Could not read library configuration {path}: {error}"
STEAM_LIBRARY_CONFIG_MISSING_OBJECT_WARNING = (
    "Library configuration has no libraryfolders object: {path}"
)
STEAM_LIBRARY_PATH_MISSING_WARNING = "Library {index} has no valid path in {path}"
STEAM_LIBRARY_PATH_NOT_ABSOLUTE_WARNING = "Library {index} path is not absolute in {path}"
STEAM_LIBRARY_UNAVAILABLE_WARNING = "Steam library is unavailable: {path}"
STEAM_LIBRARY_NO_STEAMAPPS_WARNING = "Steam library has no steamapps directory: {path}"
STEAM_MANIFEST_ENUMERATION_WARNING = "Could not enumerate manifests in {path}: {error}"
STEAM_MANIFEST_READ_WARNING = "Could not read app manifest {path}: {error}"
STEAM_MANIFEST_APP_STATE_MISSING_WARNING = "App manifest has no AppState object: {path}"
STEAM_MANIFEST_FIELDS_MISSING_WARNING = (
    "App manifest is missing appid, name, or installdir: {path}"
)
STEAM_MANIFEST_APP_ID_MISMATCH_WARNING = "App ID does not match manifest filename: {path}"
STEAM_MANIFEST_INSTALL_DIRECTORY_UNSAFE_WARNING = (
    "Unsafe install directory in app manifest: {path}"
)
STEAM_MANIFEST_INSTALL_DIRECTORY_ESCAPE_WARNING = (
    "Install directory escapes the Steam common folder: {path}"
)
STEAM_GAME_DIRECTORY_UNAVAILABLE_WARNING = "Installed game directory is unavailable: {path}"
STEAM_NOT_FOUND_WARNING = (
    "No Steam installation was found in the current user's standard locations."
)

SCAN_REPORT_LIBRARIES_KEY = "libraries_scanned"
SCAN_REPORT_APPS_KEY = "apps_scanned"
SCAN_REPORT_APP_LIST_KEY = "apps"
SCAN_REPORT_WARNINGS_KEY = "warnings"
LOG_EVENT_PATTERN = re.compile(r"[a-z][a-z0-9_.-]{0,79}\Z")
LOG_CONTEXT_VALUE_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}\Z")
LOG_INVALID_EVENT_MESSAGE = "Log event must be a lowercase event identifier"
LOG_INVALID_CONTEXT_MESSAGE = "Invalid log context value for {key}"
LOG_CONTEXT_FIELDS = ("operation_id", "game_id", "mod_id", "phase")
LOG_EVENT_FIELD_FORMAT = "event={event}"
LOG_CONTEXT_FIELD_FORMAT = "{key}={value}"
LOG_EVENT_BACKEND_STATUS_REQUESTED = "backend.status_requested"
LOG_EVENT_SUPPORTED_GAME_SCAN_STARTED = "supported_game.scan_started"
LOG_EVENT_SUPPORTED_GAME_SCAN_COMPLETED = "supported_game.scan_completed"
LOG_EVENT_SUPPORTED_GAME_SCAN_FAILED = "supported_game.scan_failed"
LOG_EVENT_PLUGIN_STARTED = "plugin.started"
LOG_EVENT_PLUGIN_STOPPED = "plugin.stopped"
LOG_SCAN_PHASE_FORMAT = "libraries-{libraries}-apps-{apps}-supported-{supported}"

BACKEND_STATUS_FIELD = "status"
BACKEND_MESSAGE_FIELD = "message"
BACKEND_STATUS_READY = "ready"
BACKEND_READY_MESSAGE = "AnyAspectRatio Decky backend is responding."

MOD_CATALOG_FILENAME = "catalog.json"
MOD_CATALOG_SCHEMA_VERSION = 1
MOD_CATALOG_SCHEMA_VERSION_KEY = "schema_version"
MOD_CATALOG_MODS_KEY = "mods"
MOD_ID_KEY = "id"
MOD_NAME_KEY = "name"
MOD_STEAM_APP_ID_KEY = "steam_app_id"
MOD_INSTALL_DIRECTORY_KEY = "install_dir"
MOD_EXECUTABLE_KEY = "executable"
MOD_RELEASE_KEY = "release"
MOD_RELEASE_VERSION_KEY = "version"
MOD_RELEASE_URL_KEY = "url"
MOD_RELEASE_SHA256_KEY = "sha256"
MOD_RELEASE_SIZE_KEY = "size_bytes"
MOD_RELEASE_MAX_SIZE_KEY = "max_size_bytes"
MOD_FILES_KEY = "files"
MOD_INI_FILE_KEY = "ini_file"
MOD_WINE_OVERRIDES_KEY = "wine_dll_overrides"

MOD_CATALOG_INVALID_JSON_MESSAGE = "Mod catalog is not valid JSON: {error}"
MOD_CATALOG_INVALID_SCHEMA_MESSAGE = "Mod catalog must contain schema_version and mods"
MOD_CATALOG_UNSUPPORTED_SCHEMA_MESSAGE = "Unsupported mod catalog schema version: {version}"
MOD_CATALOG_INVALID_ENTRY_MESSAGE = "Mod catalog entry {index} must be an object"
MOD_CATALOG_MISSING_FIELD_MESSAGE = "Mod catalog entry {mod_id} is missing {field}"
MOD_CATALOG_INVALID_APP_ID_MESSAGE = "Mod catalog entry {mod_id} has an invalid Steam app ID"
MOD_CATALOG_DUPLICATE_ID_MESSAGE = "Duplicate mod ID in catalog: {mod_id}"
MOD_CATALOG_DUPLICATE_APP_ID_MESSAGE = "Duplicate Steam app ID in catalog: {app_id}"
MOD_CATALOG_INVALID_RELEASE_MESSAGE = "Mod catalog entry {mod_id} has invalid release metadata"
MOD_CATALOG_INVALID_FILE_LIST_MESSAGE = "Mod catalog entry {mod_id} has an invalid files list"
MOD_CATALOG_INVALID_OVERRIDE_LIST_MESSAGE = (
    "Mod catalog entry {mod_id} has an invalid Wine DLL overrides list"
)
MOD_CATALOG_DUPLICATE_FILE_MESSAGE = "Mod catalog entry {mod_id} has duplicate file: {path}"
MOD_CATALOG_INVALID_RELATIVE_PATH_MESSAGE = (
    "Mod catalog entry {mod_id} has an unsafe relative path: {path}"
)
MOD_CATALOG_INVALID_URL_MESSAGE = "Mod catalog entry {mod_id} release URL must use HTTPS on github.com"
MOD_CATALOG_INVALID_HASH_MESSAGE = "Mod catalog entry {mod_id} release SHA-256 must be 64 hex characters"
MOD_CATALOG_INVALID_SIZE_MESSAGE = "Mod catalog entry {mod_id} release size limit is invalid"
MOD_CATALOG_INI_NOT_IN_FILES_MESSAGE = (
    "Mod catalog entry {mod_id} INI file must be included in files"
)
MOD_CATALOG_INSTALL_DIRECTORY_MISMATCH_WARNING = (
    "Installed app {app_id} directory {actual} does not match catalog directory {expected}"
)
MOD_CATALOG_FILE_STATUS_NONE = "not_detected"
MOD_CATALOG_FILE_STATUS_PARTIAL = "partial"
MOD_CATALOG_FILE_STATUS_COMPLETE = "complete"
MOD_CATALOG_PATH_SEPARATOR = "/"
MOD_CATALOG_UNSAFE_PATH_CHARACTERS = ("\\", ":", "\0")
MOD_CATALOG_OVERRIDE_PATTERN = re.compile(r"[a-z0-9_,-]+=[a-z,]+\Z")
MOD_CATALOG_SHA256_PATTERN = re.compile(r"[a-fA-F0-9]{64}\Z")
MOD_CATALOG_ID_PATTERN = re.compile(r"[a-z0-9][a-z0-9-]{0,63}\Z")
MOD_CATALOG_APP_ID_PATTERN = re.compile(r"\d+\Z")
MOD_CATALOG_REQUIRED_STRING_FIELDS = (
    MOD_ID_KEY,
    MOD_NAME_KEY,
    MOD_STEAM_APP_ID_KEY,
    MOD_INSTALL_DIRECTORY_KEY,
    MOD_EXECUTABLE_KEY,
    MOD_INI_FILE_KEY,
)
MOD_CATALOG_REQUIRED_RELEASE_FIELDS = (
    MOD_RELEASE_VERSION_KEY,
    MOD_RELEASE_URL_KEY,
    MOD_RELEASE_SHA256_KEY,
)
MOD_CATALOG_EXECUTABLE_FOUND_FIELD = "executable_found"
MOD_CATALOG_FILES_PRESENT_FIELD = "files_present"
MOD_CATALOG_FILES_MISSING_FIELD = "files_missing"
MOD_CATALOG_MOD_ID_FIELD = "mod_id"
MOD_CATALOG_MOD_NAME_FIELD = "mod_name"
MOD_CATALOG_EXECUTABLE_PATH_FIELD = "executable_path"
MOD_CATALOG_INI_PATH_FIELD = "ini_path"
MOD_CATALOG_RELEASE_VERSION_FIELD = "release_version"
MOD_CATALOG_GAMES_FIELD = "supported_games"
MOD_CATALOG_COUNT_FIELD = "catalogued_mods"
MOD_CATALOG_SCAN_REPORT_FIELD = "steam_scan"
MOD_CATALOG_APP_NAME_FIELD = "app_name"
MOD_CATALOG_GAME_PATH_FIELD = "game_path"
MOD_CATALOG_FILE_STATUS_FIELD = "file_status"
