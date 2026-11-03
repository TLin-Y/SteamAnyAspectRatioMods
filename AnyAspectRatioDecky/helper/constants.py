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
LOG_EVENT_STEAM_SCAN_STARTED = "steam.scan_started"
LOG_EVENT_STEAM_SCAN_COMPLETED = "steam.scan_completed"
LOG_EVENT_PLUGIN_STARTED = "plugin.started"
LOG_EVENT_PLUGIN_STOPPED = "plugin.stopped"
LOG_SCAN_PHASE_FORMAT = "libraries-{libraries}-apps-{apps}"

BACKEND_STATUS_FIELD = "status"
BACKEND_MESSAGE_FIELD = "message"
BACKEND_STATUS_READY = "ready"
BACKEND_READY_MESSAGE = "AnyAspectRatio Decky backend is responding."
