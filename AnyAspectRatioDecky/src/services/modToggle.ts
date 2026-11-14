import { setModApplied } from "./callables";
import {
  confirmFileReplacement,
  confirmModifiedIniRemoval,
  showLaunchOptionsInfo,
} from "./modals";
import type { SupportedGame } from "./types";
import {
  type LaunchOptionsPlan,
  prepareLaunchOptions,
  readCurrentLaunchOptions,
  removeLaunchOptions,
} from "../launch_options/steamLaunchOptions";

/** The launch option a user sets by hand when the plugin cannot write it. */
export const requiredLaunchOption = (game: SupportedGame) =>
  `WINEDLLOVERRIDES="${game.wine_dll_overrides.join(";")}" %command%`;

export const withRefreshError = (message: string, refreshError: string | null) =>
  refreshError ? `${message} Status refresh failed: ${refreshError}` : message;

async function currentLaunchOptions(game: SupportedGame): Promise<{ value: string; note: string }> {
  try {
    return { value: await readCurrentLaunchOptions(Number(game.steam_app_id)), note: "" };
  } catch (error) {
    return { value: "", note: `\n\nSteam's current launch options could not be read: ${String(error)}` };
  }
}

/** Plan the WINEDLLOVERRIDES change before any file is touched; a notice means it must be set by hand. */
export async function planLaunchOptions(
  game: SupportedGame,
): Promise<{ plan: LaunchOptionsPlan | null; notice: string }> {
  try {
    const plan = await prepareLaunchOptions(Number(game.steam_app_id), game.mod_id, game.wine_dll_overrides);
    if (plan && !plan.canWrite && plan.proposedOptions !== plan.currentOptions) {
      return { plan: null, notice: "Steam's launch-options write API is unavailable. Mod files will still be applied." };
    }
    return { plan, notice: "" };
  } catch (error) {
    return { plan: null, notice: String(error) };
  }
}

/**
 * Apply or remove a mod's files. Existing files that differ are confirmed with the user and the
 * call is repeated with the answer. Throws when the backend reports an error.
 */
export async function setModFiles(
  game: SupportedGame & { game_path: string },
  applied: boolean,
): Promise<{ warning?: string; conflicts: string[]; iniWarning: string }> {
  const result = await setModApplied(game.mod_id, game.game_path, applied, null, null);
  if (result.error) throw new Error(result.error);
  if (result.warning) return { warning: result.warning, conflicts: [], iniWarning: "" };
  const iniWarning = result.ini_adjustment_warning ?? "";
  if (result.conflicting_files.length === 0) return { conflicts: [], iniWarning };
  const retry = applied
    ? await setModApplied(
        game.mod_id, game.game_path, true,
        await confirmFileReplacement(game.app_name, result.conflicting_files, game.backup_files ?? []),
        null,
      )
    : await setModApplied(
        game.mod_id, game.game_path, false, null,
        await confirmModifiedIniRemoval(game.app_name, result.conflicting_files[0]),
      );
  if (retry.error) throw new Error(retry.error);
  return {
    conflicts: retry.conflicting_files,
    iniWarning: (applied ? retry.ini_adjustment_warning : undefined) ?? iniWarning,
  };
}

/** Removing the files failed: show the error together with Steam's current launch options. */
export async function showRemovalFailure(game: SupportedGame, error: unknown, refreshError: string | null) {
  const current = await currentLaunchOptions(game);
  showLaunchOptionsInfo(
    "Mod removal failed",
    withRefreshError(`Could not remove ${game.mod_name}: ${String(error)}${current.note}`, refreshError),
    [{ label: "Current Steam launch options", value: current.value }],
  );
}

/**
 * After the files are removed, drop the override groups this plugin added and show what Steam has now.
 * Returns the status-refresh error, if any.
 */
export async function removeModLaunchOptions(
  game: SupportedGame,
  refresh: () => Promise<string | null>,
): Promise<string | null> {
  try {
    const removal = await removeLaunchOptions(Number(game.steam_app_id), game.mod_id);
    const refreshError = await refresh();
    if (!removal.hadRecord) {
      showLaunchOptionsInfo("Mod files deleted successfully", "", [
        { label: "Current Steam options", value: removal.currentOptions },
      ]);
    } else {
      showLaunchOptionsInfo(
        "Mod files removed",
        removal.changed
          ? `Removed ${game.mod_name} files and the plugin-owned override group(s). Other current launch-option arguments were preserved.`
          : `Removed ${game.mod_name} files. None of the recorded plugin-owned override groups were present, so Steam launch options were left unchanged.`,
        [
          { label: "Current launch options before unApply", value: removal.currentOptions },
          { label: "Launch options after unApply", value: removal.updatedOptions },
        ],
      );
    }
    return refreshError;
  } catch (error) {
    const refreshError = await refresh();
    const current = await currentLaunchOptions(game);
    showLaunchOptionsInfo(
      "Launch options removal failed",
      withRefreshError(
        `${game.mod_name} files were removed, but Steam's launch options could not be updated: ${String(error)}${current.note}`,
        refreshError,
      ),
      [{ label: "Current Steam launch options", value: current.value }],
    );
    return null;
  }
}
