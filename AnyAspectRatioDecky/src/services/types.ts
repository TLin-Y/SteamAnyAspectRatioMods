export type SupportedGameReport = {
  steam_scan: {
    libraries_scanned: number;
    apps_scanned: number;
  };
  catalogued_mods: number;
  local_archive_count?: number;
  supported_games: {
    mod_id: string;
    mod_name: string;
    steam_app_id: string;
    app_name: string;
    game_path: string | null;
    executable_path: string;
    executable_found: boolean;
    file_status: string;
    files_present: string[];
    files_missing: string[];
    ini_path: string | null;
    ini_file: string | null;
    mod_files: string[];
    source_kind: string;
    wine_dll_overrides: string[];
    manual?: boolean;
    offline_only?: boolean;
    matched_by_executable?: boolean;
    update_available?: boolean;
    ambiguity_group?: string | null;
    match_locked?: boolean;
    log_files?: string[];
  }[];
  warnings: string[];
};

export type PluginSettingsValues = {
  catalog_repository_url: string;
  catalog_ref: string;
  catalog_path: string;
  local_archive_dir: string;
};

export type PluginSettingsInfo = {
  settings: PluginSettingsValues & { offline_mode: boolean };
  defaults: Omit<PluginSettingsValues, "local_archive_dir">;
  catalog: {
    origin: "cache" | "bundled";
    mod_count: number;
    fetched_at: string | null;
    warning: string | null;
  };
  local_archives: { count: number; warnings: string[] };
  paths: { data_dir: string; log_file: string };
};

export type PluginUpdateInfo = {
  error?: string;
  installed_version: string;
  latest_version: string;
  update_available: boolean;
  download_url: string;
};

export type CreditsInfo = {
  mod_count: number;
  offline_mode: boolean;
  authors: {
    name: string;
    url: string;
    support: { label: string; url: string }[];
    mod_count: number;
  }[];
  pack: { file_count: number; size_bytes: number };
};

export type LaunchOptionsDisplay = {
  label: string;
  value: string;
};

export type SupportedGame = SupportedGameReport["supported_games"][number];

export type ModLogResult = { name: string; content: string; truncated: boolean } | { name: string; error: string };
