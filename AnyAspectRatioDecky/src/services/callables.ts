import { callable } from "@decky/api";
import type { SupportedGameReport, PluginSettingsInfo, PluginUpdateInfo, CreditsInfo } from "./types";

export const PLUGIN_VERSION = "1.7";

export const DEFAULT_PICKER_DIRECTORY = "/home/deck/Downloads";

export const scanSupportedGames = callable<[], SupportedGameReport>("scan_supported_games");

export const getCachedScan = callable<
  [],
  { scanned_at: string; report: SupportedGameReport } | null
>("get_cached_scan");

export const getPluginSettings = callable<[], PluginSettingsInfo>("get_settings");

export const updateCurrentResolution = callable<
  [],
  { updated: string[]; unchanged: string[]; skipped: string[]; failures: string[]; warnings: string[]; resolution_update_pending: boolean }
>("update_current_resolution");

export const savePluginSettings = callable<
  [string, string, string, string, string, string],
  { saved: boolean; source_changed: boolean }
>("save_settings");

export const setOfflineMode = callable<[boolean], { offline_mode: boolean; warning?: string | null }>("set_offline_mode");

export const checkForPluginUpdate = callable<[], PluginUpdateInfo>("check_plugin_update");

export const installPluginUpdateRequest = callable<
  [],
  {
    error?: string;
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
    error?: string;
    catalog_changed: boolean;
    mod_count: number;
    fetched_at: string;
    local_archive_count: number;
    warnings: string[];
  }
>("update_dependencies");

export const getCredits = callable<[], CreditsInfo>("get_credits");

export const importOfflinePack = callable<[string], { error?: string; warning?: string | null }>("import_offline_pack");

export const downloadOfflinePack = callable<[], { error?: string; version?: string; warning?: string | null }>("download_offline_pack");

export const clearLocalData = callable<[], { error?: string }>("clear_local_data");

export const setModApplied = callable<
  [string, string, boolean, boolean | null, boolean | null],
  {
    applied: boolean;
    copied_file_count: number;
    conflicting_files: string[];
    ini_adjustment_warning?: string;
    error?: string;
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

export const getLog = callable<[number], { content?: string; path?: string; error?: string }>(
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
