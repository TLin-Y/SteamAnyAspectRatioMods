import {
  ButtonItem,
  PanelSection,
  PanelSectionRow,
} from "@decky/ui";
import { Fragment, useEffect, useRef, useState } from "react";
import { GameDetails } from "./GameDetails";
import { GameCard } from "./GameCard";
import { useIniEditor } from "./useIniEditor";

import {
  scanSupportedGames,
  getCachedScan,
  clearMatchedGame,
  getModLog,
  getModReadme,
  selectMatchedGame,
  getDiskUsage,
} from "../../services/callables";
import {
  confirmOfflineMod,
  showErrorModal,
  showLaunchOptionsInfo,
  chooseGame,
} from "../../services/modals";
import { formatBytes, formatRelativeTime } from "../../services/formatters";
import type { SupportedGameReport, SupportedGame, ModLogResult } from "../../services/types";
import { applyLaunchOptions } from "../../launch_options/steamLaunchOptions";
import {
  planLaunchOptions,
  removeModLaunchOptions,
  requiredLaunchOption,
  setModFiles,
  showRemovalFailure,
  withRefreshError,
} from "../../services/modToggle";

const gameKey = (game: SupportedGame) => `${game.steam_app_id}-${game.game_path}-${game.mod_id}`;

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
  const [detailView, setDetailView] = useState<"manage" | "readme" | "log">("manage");
  const [readmeTitle, setReadmeTitle] = useState("");
  const [readmeContent, setReadmeContent] = useState("");
  const [readmeLoading, setReadmeLoading] = useState(false);
  const [modLogs, setModLogs] = useState<ModLogResult[] | null>(null);
  const detailRequestId = useRef(0);
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

  /** Rescan to refresh the game list; returns the error text instead of throwing. */
  const refresh = async (): Promise<string | null> => {
    try {
      setReport(await scanSupportedGames());
      setCachedScanTime(new Date().toISOString());
      return null;
    } catch (error) {
      return String(error);
    }
  };

  const markUpToDate = (game: SupportedGame) =>
    setReport((current) => current && {
      ...current,
      supported_games: current.supported_games.map((candidate) =>
        candidate.mod_id === game.mod_id && candidate.game_path === game.game_path
          ? { ...candidate, update_available: false }
          : candidate,
      ),
    });

  /** Forget the user's pick in every ambiguity group that pointed at this game. */
  const forgetChoice = (game: SupportedGame) =>
    setAmbiguitySelections((current) => {
      const updated = { ...current };
      if (game.ambiguity_group) delete updated[game.ambiguity_group];
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

  /** Apply or remove a mod: files first, then the Steam launch options it needs. */
  const changeModApplied = async (game: SupportedGame, applied: boolean) => {
    if (!game.game_path) return;
    const target = { ...game, game_path: game.game_path };
    const hasOverrides = game.wine_dll_overrides.length > 0;
    setUpdatingMod(true);
    setErrorMessage("");
    try {
      if (applied && game.offline_only && !await confirmOfflineMod(game.app_name)) return;
      const launch = applied && hasOverrides ? await planLaunchOptions(game) : { plan: null, notice: "" };

      let outcome: Awaited<ReturnType<typeof setModFiles>>;
      try {
        outcome = await setModFiles(target, applied);
      } catch (error) {
        const refreshError = await refresh();
        if (!applied && hasOverrides) {
          await showRemovalFailure(game, error, refreshError);
        } else {
          const action = applied ? "apply" : "remove";
          notifyError(
            `${applied ? "Apply" : "Remove"} failed`,
            withRefreshError(`Could not ${action} ${game.mod_name}: ${String(error)}`, refreshError),
          );
        }
        return;
      }
      if (outcome.warning) {
        showErrorModal("Mod removal needs attention", outcome.warning);
        await refresh();
        return;
      }
      if (outcome.conflicts.length === 0) {
        if (applied) markUpToDate(game);
        else forgetChoice(game);
      }
      if (outcome.iniWarning) setErrorMessage(outcome.iniWarning);

      if (!applied) {
        const refreshError = hasOverrides ? await removeModLaunchOptions(game, refresh) : await refresh();
        if (refreshError && hasOverrides) setErrorMessage(`Status refresh failed: ${refreshError}`);
        else if (refreshError) {
          notifyError("Status refresh failed", `Operation completed, but refreshing status failed: ${refreshError}`);
        }
        return;
      }
      let notice = launch.notice;
      if (launch.plan) {
        try {
          await applyLaunchOptions(launch.plan, game.wine_dll_overrides);
        } catch (error) {
          notice = `Steam launch options could not be updated: ${String(error)}. Mod files were still applied.`;
        }
      }
      const refreshError = await refresh();
      if (notice) {
        const setting = requiredLaunchOption(game);
        showLaunchOptionsInfo(
          "Launch options not updated",
          withRefreshError(`${notice}

Required setting: ${setting}`, refreshError),
          [{ label: "Required Steam launch option", value: setting }],
        );
      } else if (refreshError) {
        notifyError("Status refresh failed", `Operation completed, but refreshing status failed: ${refreshError}`);
      }
    } finally {
      setToggleRevision((revision) => revision + 1);
      setUpdatingMod(false);
    }
  };

  const selectedGame = report?.supported_games.find(
    (game) => gameKey(game) === selectedGameKey,
  );
  const ini = useIniEditor({
    game: selectedGame, gameKey, requestId: detailRequestId, refresh,
    notifyError: (title, message) => notifyError(title, message),
  });
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

  const openGameDetails = async (game: SupportedGame) => {
    const requestId = ++detailRequestId.current;
    setSelectedGameKey(gameKey(game));
    setDetailView("manage");
    await ini.open(game, requestId);
  };

  const openGameReadme = async (
    game: SupportedGame,
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
      const refreshError = await refresh();
      if (refreshError) {
        setReport(null);
        notifyError("Status refresh failed", `Choice cleared; scan again: ${refreshError}`);
      }
    } catch (error) {
      notifyError("Could not clear game choice", String(error));
    } finally {
      setUpdatingMod(false);
    }
  };



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
          ini={ini.state}
          onBack={() => {
            detailRequestId.current += 1;
            ini.close();
            setSelectedGameKey(null);
            setDetailView("manage");
            setReadmeLoading(false);
          }}
          onIniBlur={ini.onBlur}
          onIniChange={ini.onChange}
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
