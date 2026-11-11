import {
  ButtonItem,
  PanelSection,
  PanelSectionRow,
} from "@decky/ui";
import { Fragment, useEffect, useRef, useState } from "react";
import { GameDetails } from "./GameDetails";
import { GameCard } from "./GameCard";

import {
  scanSupportedGames,
  getCachedScan,
  setModApplied,
  clearMatchedGame,
  getModLog,
  getModIni,
  saveModIni,
  getModReadme,
  selectMatchedGame,
  getDiskUsage,
} from "../../services/callables";
import {
  confirmFileReplacement,
  confirmOfflineMod,
  confirmModifiedIniRemoval,
  showErrorModal,
  showLaunchOptionsInfo,
  chooseGame,
} from "../../services/modals";
import { formatBytes, formatRelativeTime } from "../../services/formatters";
import type { SupportedGameReport, SupportedGame, ModLogResult } from "../../services/types";
import {
  prepareLaunchOptions,
  readCurrentLaunchOptions,
  applyLaunchOptions,
  removeLaunchOptions,
} from "../../launch_options/steamLaunchOptions";

function GamesTab({ dataRevision }: { dataRevision: number }) {
  const [report, setReport] = useState<SupportedGameReport | null>(null);
  const [cachedScanTime, setCachedScanTime] = useState("");
  const [pluginUsageInfo, setPluginUsageInfo] = useState<string>("");
  const [clockTick, setClockTick] = useState(0);
  const [errorMessage, setErrorMessage] = useState("");
  const [scanning, setScanning] = useState(false);
  const [updatingMod, setUpdatingMod] = useState(false);
  const [toggleRevision, setToggleRevision] = useState(0);
  const [selectedGameKey, setSelectedGameKey] = useState<string | null>(null);
  const [ambiguitySelections, setAmbiguitySelections] = useState<Record<string, string>>({});
  const [iniGameKey, setIniGameKey] = useState<string | null>(null);
  const [detailView, setDetailView] = useState<"manage" | "readme" | "log">("manage");
  const [readmeTitle, setReadmeTitle] = useState("");
  const [readmeContent, setReadmeContent] = useState("");
  const [readmeLoading, setReadmeLoading] = useState(false);
  const [modLogs, setModLogs] = useState<ModLogResult[] | null>(null);
  const [iniLines, setIniLines] = useState<string[]>([]);
  const [savedIniContent, setSavedIniContent] = useState("");
  const [iniExists, setIniExists] = useState(false);
  const [iniLoaded, setIniLoaded] = useState(false);
  const [iniSaving, setIniSaving] = useState(false);
  const [iniStatus, setIniStatus] = useState("");
  const detailRequestId = useRef(0);
  const iniSavingRef = useRef(false);
  const pendingIniSave = useRef(false);
  const saveIniRef = useRef<() => Promise<void>>(async () => {});
  const initialRevision = useRef(dataRevision);
  const scanStarted = useRef(false);

  useEffect(() => {
    let cancelled = false;
    getCachedScan()
      .then((cached) => {
        if (cancelled || !cached || scanStarted.current) return;
        setReport((current) => current ?? cached.report);
        setCachedScanTime(cached.scanned_at);
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    let cancelled = false;
    getDiskUsage()
      .then((bytes) => {
        if (cancelled) return;
        setPluginUsageInfo(`Plugin disk usage: ${formatBytes(bytes)}`);
      })
      .catch((error) => {
        if (!cancelled) setPluginUsageInfo(`Disk usage unavailable: ${String(error)}`);
      });
    return () => {
      cancelled = true;
    };
  }, [dataRevision, cachedScanTime, clockTick]);

  useEffect(() => {
    const timer = window.setInterval(() => setClockTick((tick) => tick + 1), 60_000);
    return () => window.clearInterval(timer);
  }, []);

  useEffect(() => {
    if (dataRevision === initialRevision.current) return;
    initialRevision.current = dataRevision;
    detailRequestId.current += 1;
    setSelectedGameKey(null);
    void scanForSupportedGames();
  }, [dataRevision]);

  const notifyError = (title: string, message: string) => {
    setErrorMessage(message);
    showErrorModal(title, message);
  };

  const scanForSupportedGames = async () => {
    scanStarted.current = true;
    setScanning(true);
    setReport(null);
    setCachedScanTime("");
    setErrorMessage("");
    try {
      setReport(await scanSupportedGames());
      setCachedScanTime(new Date().toISOString());
    } catch (error) {
      notifyError("Steam scan failed", `Supported game scan failed: ${String(error)}`);
    } finally {
      setScanning(false);
    }
  };

  const changeModApplied = async (
    game: SupportedGameReport["supported_games"][number],
    applied: boolean,
  ) => {
    if (!game.game_path) return;
    setUpdatingMod(true);
    setErrorMessage("");
    const refreshReport = async () => {
      try {
        setReport(await scanSupportedGames());
        setCachedScanTime(new Date().toISOString());
        return null;
      } catch (error) {
        return String(error);
      }
    };

    try {
      if (applied && game.offline_only && !await confirmOfflineMod(game.app_name)) {
        return;
      }
      let launchPlan: Awaited<ReturnType<typeof prepareLaunchOptions>> | null =
        null;
      let launchOptionsNotice = "";
      let manualLaunchOptions = "";
      if (applied && game.wine_dll_overrides.length > 0) {
        try {
          launchPlan = await prepareLaunchOptions(
            Number(game.steam_app_id),
            game.mod_id,
            game.wine_dll_overrides,
          );
        } catch (error) {
          manualLaunchOptions =
            `WINEDLLOVERRIDES="${game.wine_dll_overrides.join(";")}" %command%`;
          launchOptionsNotice =
            `${String(error)}\n\nRequired setting: ${manualLaunchOptions}`;
        }
        if (
          launchPlan &&
          !launchPlan.canWrite &&
          launchPlan.proposedOptions !== launchPlan.currentOptions
        ) {
          launchOptionsNotice =
            "Steam's launch-options write API is unavailable. Mod files will still be applied.";
          manualLaunchOptions =
            `WINEDLLOVERRIDES="${game.wine_dll_overrides.join(";")}" %command%`;
          launchPlan = null;
        }
      }

      try {
        const result = await setModApplied(
          game.mod_id,
          game.game_path,
          applied,
          null,
          null,
        );
        if (result.error) throw new Error(result.error);
        if (result.warning) {
          showErrorModal("Mod removal needs attention", result.warning);
          await refreshReport();
          return;
        }
        let iniAdjustmentWarning = result.ini_adjustment_warning ?? "";
        let remainingConflicts = result.conflicting_files;
        if (result.conflicting_files.length > 0) {
          if (applied) {
            const replaceFiles = await confirmFileReplacement(
              game.app_name,
              result.conflicting_files,
              game.backup_files ?? [],
            );
            const retry = await setModApplied(
              game.mod_id,
              game.game_path,
              true,
              replaceFiles,
              null,
            );
            if (retry.error) throw new Error(retry.error);
            iniAdjustmentWarning = retry.ini_adjustment_warning ?? iniAdjustmentWarning;
            remainingConflicts = retry.conflicting_files;
          } else {
            const keepIni = await confirmModifiedIniRemoval(
              game.app_name,
              result.conflicting_files[0],
            );
            const retry = await setModApplied(
              game.mod_id,
              game.game_path,
              false,
              null,
              keepIni,
            );
            if (retry.error) throw new Error(retry.error);
            remainingConflicts = retry.conflicting_files;
          }
        }
        if (applied && remainingConflicts.length === 0) {
          setReport((current) => current && {
            ...current,
            supported_games: current.supported_games.map((candidate) =>
              candidate.mod_id === game.mod_id && candidate.game_path === game.game_path
                ? { ...candidate, update_available: false }
                : candidate,
            ),
          });
        }
        if (iniAdjustmentWarning) {
          setErrorMessage(iniAdjustmentWarning);
        }
        const ambiguityGroup = game.ambiguity_group;
        if (!applied && remainingConflicts.length === 0) {
          setAmbiguitySelections((current) => {
            const updated = { ...current };
            if (ambiguityGroup) delete updated[ambiguityGroup];
            for (const candidate of report?.supported_games ?? []) {
              const group = candidate.ambiguity_group;
              if (
                group && candidate.mod_id === game.mod_id &&
                candidate.game_path === game.game_path &&
                current[group] === gameKey(candidate)
              ) delete updated[group];
            }
            return updated;
          });
        }
      } catch (error) {
        const refreshError = await refreshReport();
        if (!applied && game.wine_dll_overrides.length > 0) {
          let currentOptions = "";
          let currentOptionsMessage = "";
          try {
            currentOptions = await readCurrentLaunchOptions(
              Number(game.steam_app_id),
            );
          } catch (readError) {
            currentOptionsMessage = `Steam's current launch options could not be read: ${String(readError)}`;
          }
          showLaunchOptionsInfo(
            "Mod removal failed",
            `Could not remove ${game.mod_name}: ${String(error)}` +
              (currentOptionsMessage ? `\n\n${currentOptionsMessage}` : "") +
              (refreshError ? ` Status refresh failed: ${refreshError}` : ""),
            [
              {
                label: "Current Steam launch options",
                value: currentOptions,
              },
            ],
          );
          return;
        }
        notifyError(
          `${applied ? "Apply" : "Remove"} failed`,
          `Could not ${applied ? "apply" : "remove"} ${game.mod_name}: ${String(error)}` +
            (refreshError ? ` Status refresh failed: ${refreshError}` : ""),
        );
        return;
      }

      if (applied && launchPlan) {
        try {
          await applyLaunchOptions(
            launchPlan,
            game.wine_dll_overrides,
          );
        } catch (error) {
          launchOptionsNotice =
            `Steam launch options could not be updated: ${String(error)}. Mod files were still applied.`;
          manualLaunchOptions =
            `WINEDLLOVERRIDES="${game.wine_dll_overrides.join(";")}" %command%`;
        }
      } else if (!applied && game.wine_dll_overrides.length > 0) {
        try {
          const removal = await removeLaunchOptions(
            Number(game.steam_app_id),
            game.mod_id,
          );
          if (!removal.hadRecord) {
            const refreshError = await refreshReport();
            showLaunchOptionsInfo(
              "Mod files deleted successfully",
              "",
              [
                {
                  label: "Current Steam options",
                  value: removal.currentOptions,
                },
              ],
            );
            if (refreshError) {
              setErrorMessage(`Status refresh failed: ${refreshError}`);
            }
            return;
          }
          const refreshError = await refreshReport();
          showLaunchOptionsInfo(
            "Mod files removed",
            removal.changed
              ? `Removed ${game.mod_name} files and the plugin-owned override group(s). Other current launch-option arguments were preserved.`
              : `Removed ${game.mod_name} files. None of the recorded plugin-owned override groups were present, so Steam launch options were left unchanged.`,
            [
              {
                label: "Current launch options before unApply",
                value: removal.currentOptions,
              },
              {
                label: "Launch options after unApply",
                value: removal.updatedOptions,
              },
            ],
          );
          if (refreshError) {
            setErrorMessage(`Status refresh failed: ${refreshError}`);
          }
          return;
        } catch (error) {
          const refreshError = await refreshReport();
          let currentOptions = "";
          let currentOptionsMessage = "";
          try {
            currentOptions = await readCurrentLaunchOptions(
              Number(game.steam_app_id),
            );
          } catch (readError) {
            currentOptionsMessage = `Steam's current launch options could not be read: ${String(readError)}`;
          }
          showLaunchOptionsInfo(
            "Launch options removal failed",
            `${game.mod_name} files were removed, but Steam's launch options could not be updated: ${String(error)}` +
              (currentOptionsMessage ? `\n\n${currentOptionsMessage}` : "") +
              (refreshError ? ` Status refresh failed: ${refreshError}` : ""),
            [
              {
                label: "Current Steam launch options",
                value: currentOptions,
              },
            ],
          );
          return;
        }
      }

      const refreshError = await refreshReport();
      if (applied && launchOptionsNotice) {
        showLaunchOptionsInfo(
          "Launch options not updated",
          launchOptionsNotice +
            (manualLaunchOptions
              ? `\n\nRequired setting: ${manualLaunchOptions}`
              : "") +
            (refreshError ? ` Status refresh failed: ${refreshError}` : ""),
          manualLaunchOptions
            ? [{ label: "Required Steam launch option", value: manualLaunchOptions }]
            : [],
        );
        return;
      }
      if (refreshError) {
        notifyError(
          "Status refresh failed",
          `Operation completed, but refreshing status failed: ${refreshError}`,
        );
      }
    } finally {
      setToggleRevision((revision) => revision + 1);
      setUpdatingMod(false);
    }
  };

  const gameKey = (game: SupportedGameReport["supported_games"][number]) =>
    `${game.steam_app_id}-${game.game_path}-${game.mod_id}`;
  const selectedGame = report?.supported_games.find(
    (game) => gameKey(game) === selectedGameKey,
  );
  const gameGroups: SupportedGameReport["supported_games"][] = [];
  const ambiguityGroups = new Map<string, SupportedGameReport["supported_games"]>();
  for (const game of [...(report?.supported_games ?? [])].sort(
    (a, b) => Number(!!a.manual) - Number(!!b.manual),
  )) {
    if (!game.ambiguity_group) {
      gameGroups.push([game]);
      continue;
    }
    let group = ambiguityGroups.get(game.ambiguity_group);
    if (!group) {
      group = [];
      ambiguityGroups.set(game.ambiguity_group, group);
      gameGroups.push(group);
    }
    group.push(game);
  }

  const openGameDetails = async (
    game: SupportedGameReport["supported_games"][number],
  ) => {
    const requestId = ++detailRequestId.current;
    const selectedKey = gameKey(game);
    iniSavingRef.current = false;
    pendingIniSave.current = false;
    setSelectedGameKey(selectedKey);
    setDetailView("manage");
    setIniGameKey(null);
    setIniLines([]);
    setSavedIniContent("");
    setIniExists(false);
    setIniLoaded(false);
    setIniSaving(false);
    setIniStatus("");
    if (!game.ini_file || !game.game_path) return;

    setIniGameKey(selectedKey);
    try {
      const ini = await getModIni(game.mod_id, game.game_path);
      if (requestId !== detailRequestId.current) return;
      setIniLines(ini.content.split(/\r\n|\n|\r/));
      setSavedIniContent(ini.content);
      setIniExists(ini.exists);
      setIniLoaded(true);
    } catch (error) {
      if (requestId === detailRequestId.current) {
        notifyError("Could not load configuration", String(error));
      }
    }
  };

  const openGameReadme = async (
    game: SupportedGameReport["supported_games"][number],
  ) => {
    const requestId = ++detailRequestId.current;
    setSelectedGameKey(gameKey(game));
    setDetailView("readme");
    setReadmeTitle("");
    setReadmeContent("");
    setReadmeLoading(true);
    try {
      const readme = await getModReadme(game.mod_id);
      if (requestId !== detailRequestId.current) return;
      setReadmeTitle(readme.title);
      setReadmeContent(readme.content);
    } catch (error) {
      if (requestId === detailRequestId.current) {
        notifyError("Could not load ReadMe", String(error));
      }
    } finally {
      if (requestId === detailRequestId.current) {
        setReadmeLoading(false);
      }
    }
  };

  const openGameLog = async (game: SupportedGame) => {
    if (!game.game_path) return;
    const requestId = ++detailRequestId.current;
    setSelectedGameKey(gameKey(game));
    setDetailView("log");
    setModLogs(null);
    const gamePath = game.game_path;
    const results = await Promise.all((game.log_files ?? []).map(async (name): Promise<ModLogResult> => {
      try {
        return await getModLog(game.mod_id, gamePath, name);
      } catch (error) {
        return { name, error: String(error) };
      }
    }));
    if (requestId === detailRequestId.current) setModLogs(results);
  };

  const resetGameChoice = async (game: SupportedGame) => {
    const group = game.ambiguity_group;
    if (!group || !game.game_path) return;
    setUpdatingMod(true);
    try {
      await clearMatchedGame(group, game.mod_id, game.game_path);
      setAmbiguitySelections((current) => {
        const updated = { ...current };
        delete updated[group];
        return updated;
      });
      try {
        setReport(await scanSupportedGames());
        setCachedScanTime(new Date().toISOString());
      } catch (error) {
        setReport(null);
        notifyError("Status refresh failed", `Choice cleared; scan again: ${String(error)}`);
      }
    } catch (error) {
      notifyError("Could not clear game choice", String(error));
    } finally {
      setUpdatingMod(false);
    }
  };

  const saveIni = async () => {
    const gameBeingSaved = selectedGame;
    if (
      !gameBeingSaved?.game_path ||
      !gameBeingSaved.ini_file ||
      iniGameKey !== gameKey(gameBeingSaved)
    ) return;
    if (iniSavingRef.current) {
      pendingIniSave.current = true;
      return;
    }
    if (iniLines.join("\n") === savedIniContent) return;
    const requestId = detailRequestId.current;
    const gamePath = gameBeingSaved.game_path;
    const iniContent = iniLines.join("\n");
    iniSavingRef.current = true;
    setIniSaving(true);
    setIniStatus("");
    try {
      const result = await saveModIni(
        gameBeingSaved.mod_id,
        gamePath,
        iniContent,
      );
      if (requestId !== detailRequestId.current) return;
      setSavedIniContent(iniContent);
      setIniExists(true);
      setIniStatus(result.permissions_preserved
        ? "Configuration saved."
        : "Configuration saved, but this filesystem could not preserve the file permissions.");
    } catch (error) {
      if (requestId === detailRequestId.current) {
        notifyError("Could not save configuration", String(error));
      }
      return;
    } finally {
      if (requestId === detailRequestId.current) {
        iniSavingRef.current = false;
        setIniSaving(false);
        if (pendingIniSave.current) {
          pendingIniSave.current = false;
          window.setTimeout(() => void saveIniRef.current(), 0);
        }
      }
    }

    try {
      setReport(await scanSupportedGames());
      setCachedScanTime(new Date().toISOString());
    } catch (error) {
      if (requestId === detailRequestId.current) {
        notifyError(
          "Configuration saved, but status refresh failed",
          String(error),
        );
      }
    }
  };
  saveIniRef.current = saveIni;


  return (
    <PanelSection>
      {!selectedGame && (
        <PanelSectionRow>
          <ButtonItem
            layout="below"
            onClick={scanForSupportedGames}
            disabled={scanning || updatingMod}
          >
            {scanning ? "Scanning Steam libraries..." : "Scan for supported games"}
          </ButtonItem>
        </PanelSectionRow>
      )}
      {!selectedGame && (
        <PanelSectionRow>
          <div role="status">
            {errorMessage ||
              (report
                ? `Found ${gameGroups.length} supported games.${cachedScanTime ? ` Last scanned ${formatRelativeTime(cachedScanTime)}.` : ""}`
                : "Steam libraries have not been scanned.")}
          </div>
        </PanelSectionRow>
      )}
      {!selectedGame && pluginUsageInfo && (
        <PanelSectionRow>
          <div role="status">{pluginUsageInfo}</div>
        </PanelSectionRow>
      )}
      {report?.supported_games.length === 0 && (
        <PanelSectionRow>
          <div role="status">No catalogued games were found in the scanned Steam libraries.</div>
        </PanelSectionRow>
      )}
      {selectedGame ? (
        <GameDetails
          game={selectedGame}
          view={detailView}
          readme={{ title: readmeTitle, content: readmeContent, loading: readmeLoading }}
          logs={modLogs}
          ini={{ lines: iniLines, loaded: iniLoaded, exists: iniExists, saving: iniSaving, status: iniStatus }}
          onBack={() => {
            detailRequestId.current += 1;
            iniSavingRef.current = false;
            pendingIniSave.current = false;
            setSelectedGameKey(null);
            setDetailView("manage");
            setIniGameKey(null);
            setIniSaving(false);
            setReadmeLoading(false);
          }}
          onIniBlur={() => {
            if (!iniLoaded) return;
            if (iniSavingRef.current) {
              pendingIniSave.current = true;
            } else if (iniLines.join("\n") !== savedIniContent) {
              void saveIni();
            }
          }}
          onIniChange={(index, value) => {
            const updatedLines = [...iniLines];
            updatedLines[index] = value;
            setIniLines(updatedLines);
            setIniStatus("");
          }}
        />
      ) : gameGroups.map((group, index, ordered) => {

        const first = group[0];
        const groupId = first.ambiguity_group;
        const game = first.match_locked ? first : groupId
          ? group.find((candidate) => gameKey(candidate) === ambiguitySelections[groupId])
          : first;
        const selectGame = async () => {
          if (!groupId) return;
          const chosen = await chooseGame(group);
          if (!chosen) {
            setToggleRevision((revision) => revision + 1);
            return;
          }
          if (!chosen.game_path) {
            notifyError("Game selection failed", "The selected game folder is unavailable. Scan again and retry.");
            return;
          }
          try {
            await selectMatchedGame(groupId, chosen.mod_id, chosen.game_path);
          } catch (error) {
            notifyError("Game selection failed", String(error));
            setToggleRevision((revision) => revision + 1);
            return;
          }
          setAmbiguitySelections((current) => ({ ...current, [groupId]: gameKey(chosen) }));
          if (!chosen.manual) {
            await changeModApplied(chosen, true);
          } else {
            await openGameReadme(chosen);
          }
        };
        return (
          <Fragment key={groupId ?? gameKey(first)}>
            {first.manual && !ordered[index - 1]?.[0].manual && (
              <PanelSection title="Manual fixes">
                <PanelSectionRow>
                  <div>These fixes cannot be installed automatically. Open the ReadMe for the steps.</div>
                </PanelSectionRow>
              </PanelSection>
            )}
            <GameCard
              first={first}
              game={game}
              index={index}
              groupId={groupId}
              toggleRevision={toggleRevision}
              updating={updatingMod}
              readmeLoading={readmeLoading}
              onSelect={() => void selectGame()}
              onApply={(selected, applied) => void changeModApplied(selected, applied)}
              onClear={(selected) => void resetGameChoice(selected)}
              onEdit={(selected) => void openGameDetails(selected)}
              onReadme={(selected) => void openGameReadme(selected)}
              onLog={(selected) => void openGameLog(selected)}
            />
          </Fragment>
        );
      })}

      {!selectedGame && report?.warnings
        .filter((warning) => !warning.startsWith("Steam library is unavailable:"))
        .map((warning, index) => (
          <PanelSectionRow key={`${index}-${warning}`}>
            <div role="status">{warning}</div>
          </PanelSectionRow>
        ))}
    </PanelSection>
  );
}

export default GamesTab;
