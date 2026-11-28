import { callable } from "@decky/api";
import type { SupportedGameReport, PluginSettingsInfo, PluginUpdateInfo, CreditsInfo } from "./types";

// Must match "name" in plugin.json and PLUGIN_NAME in helper/__init__.py.
export const PLUGIN_NAME = "AnyAspectRatio";
export const PLUGIN_VERSION = "2.2";

export const DEFAULT_PICKER_DIRECTORY = "/home/deck/Downloads";

export const scanSupportedGames = callable<[], SupportedGameReport>("scan_supported_games");

export const getCachedScan = callable<
  [],
  { scanned_at: string; report: SupportedGameReport } | null
>("get_cached_scan");

export const getPluginSettings = callable<[], PluginSettingsInfo>("get_settings");

export const updateCurrentResolution = callable<
  [],
  { updated: string[]; unchanged: string[]; skipped: string[]; failures: string[]; resolution_update_pending: boolean }
>("update_current_resolution");

export const savePluginSettings = callable<
  [string, string, string, string, string, string],
  { saved: boolean; source_changed: boolean }
>("save_settings");

export const addCustomGameDir = callable<[string], { custom_game_dirs: string[] }>("add_custom_game_dir");

export const removeCustomGameDir = callable<[string], { custom_game_dirs: string[] }>("remove_custom_game_dir");

export const setOfflineMode = callable<[boolean], { offline_mode: boolean; warning?: string | null }>("set_offline_mode");

export const checkForPluginUpdate = callable<[], PluginUpdateInfo>("check_plugin_update");

export const installPluginUpdateRequest = callable<
  [],
  {
    installed?: boolean;
    version?: string;
    restarted?: boolean;
    saved_zip?: string;
    file_name?: string;
    size_bytes?: number;
  }
>("install_plugin_update");

export const updateDependencies = callable<
  [],
  {
    catalog_changed: boolean;
    mod_count: number;
    fetched_at: string;
    local_archive_count: number;
    warnings: string[];
  }
>("update_dependencies");

export const getCredits = callable<[], CreditsInfo>("get_credits");

export const importOfflinePack = callable<[string], { warning?: string | null }>("import_offline_pack");

export const importAuthorDownload = callable<[string, string], Record<string, never>>("import_author_download");

export const getDownloadsFolder = callable<[], string>("get_downloads_folder");


export const clearLocalData = callable<[boolean, boolean, boolean], Record<string, never>>("clear_local_data");

export const setModApplied = callable<
  [string, string, boolean, boolean | null, boolean | null],
  {
    applied: boolean;
    copied_file_count: number;
    conflicting_files: string[];
    ini_adjustment_warning?: string;
    warning?: string;
  }
>("set_mod_applied");

export const selectMatchedGame = callable<[string, string, string], { selected: boolean }>(
  "select_matched_game",
);

export const clearMatchedGame = callable<[string, string, string], { cleared: boolean }>(
  "clear_matched_game",
);

export const getModLog = callable<
  [string, string, string],
  { name: string; content: string; truncated: boolean }
>("get_mod_log");

export const getOfflineMode = callable<[], boolean>("get_offline_mode");

export const getDiskUsage = callable<[], number>("get_disk_usage");

export const getLog = callable<[number], { content: string; path: string }>(
  "get_log",
);

export const getModIni = callable<
  [string, string],
  { ini_file: string; content: string; exists: boolean }
>("get_mod_ini");

export const saveModIni = callable<
  [string, string, string],
  { saved: boolean; ini_file: string | null; permissions_preserved: boolean }
>("save_mod_ini");

export const getModReadme = callable<[string], { title: string; content: string }>(
  "get_mod_readme",
);
