from typing import Annotated

from helper.validator import Validator
from utils.json_codec import Check, empty_or

_SAFE_PATH = Check(Validator.Paths.is_relative, "a safe relative path")
_HTTPS_URL = Check(Validator.Urls.is_https, "an https URL")
_SHA256 = Check(Validator.Hash.sha256, "a SHA-256 hex digest")

NonEmpty = Annotated[str, Check(bool, "a non-empty string")]
NonBlank = Annotated[str, Check(lambda value: bool(value.strip()), "a non-blank string")]
RelativePath = Annotated[str, _SAFE_PATH]
RelativePathOrEmpty = Annotated[str, empty_or(_SAFE_PATH)]
HttpsUrl = Annotated[str, _HTTPS_URL]
HttpsUrlOrEmpty = Annotated[str, empty_or(_HTTPS_URL)]
Sha256 = Annotated[str, _SHA256]
Sha256OrEmpty = Annotated[str, empty_or(_SHA256)]
ModId = Annotated[str, Check(Validator.Catalog.mod_id, "lowercase letters, digits and -")]
SteamAppId = Annotated[str, Check(Validator.Catalog.steam_app_id, "a numeric Steam app ID")]
DisplayTag = Annotated[str, Check(Validator.Catalog.display_tag, "lowercase letters, digits, _ and -")]
WineOverride = Annotated[str, Check(Validator.Wine.override, "a Wine DLL override like dinput8=n,b")]
IniSection = Annotated[str, Check(Validator.Ini.section, "an INI section name")]
IniKey = Annotated[str, Check(Validator.Ini.key, "an INI key name")]
GitHubRepository = Annotated[str, Check(Validator.GitHub.repository, "https://github.com/<owner>/<repo>")]
GitRef = Annotated[str, Check(Validator.GitHub.ref, "a git branch, tag or commit")]
