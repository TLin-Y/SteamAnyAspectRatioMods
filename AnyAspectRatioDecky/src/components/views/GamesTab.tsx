import {
  ButtonItem,
  PanelSection,
  PanelSectionRow,
} from "@decky/ui";
import { Fragment, useEffect, useRef, useState } from "react";
import { GameDetails, LogText } from "./GameDetails";
import { GameCard } from "./GameCard";
import { GameFilter } from "./GameFilter";
import { useIniEditor } from "./useIniEditor";
import { gameMatchesFilter } from "../../services/gameFilter";

import {
  scanSupportedGames,
  getCachedScan,
  clearMatchedGame,
  getModLog,
  getModReadme,
  selectMatchedGame,
  getDiskUsage,
  getLog,
} from "../../services/callables";
import {
  confirmOfflineMod,
  showErrorModal,
  showLaunchOptionsInfo,
  chooseGame,
} from "../../services/modals";
import { errorText, formatBytes, formatRelativeTime } from "../../services/formatters";
import { useRemembered } from "../../services/remembered";
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
  const [scanLog, setScanLog] = useState("");
  const [scanOutdated, setScanOutdated] = useRemembered("games.scanOutdated", false);
  // The step a running mod change is on, shown on that game's card; null when nothing runs.
  const [modProgress, setModProgress] = useState<{ key: string; step: string } | null>(null);
  const updatingMod = modProgress !== null;
  const [toggleRevision, setToggleRevision] = useState(0);
  const [selectedGameKey, setSelectedGameKey] = useRemembered<string | null>("games.selected", null);
  const [ambiguitySelections, setAmbiguitySelections] = useState<Record<string, string>>({});
  const [detailView, setDetailView] = useRemembered<"manage" | "readme" | "log">("games.detailView", "manage");
  const [readmeTitle, setReadmeTitle] = useState("");
  const [readmeContent, setReadmeContent] = useState("");
  const [readmeLoading, setReadmeLoading] = useState(false);
  const [modLogs, setModLogs] = useState<ModLogResult[] | null>(null);
  const [filter, setFilter] = useRemembered("games.filter", "");
  const detailRequestId = useRef(0);
  const initialRevision = useRef(dataRevision);
  const scanStarted = useRef(false);
  // After a remount the remembered game's details (INI, ReadMe or log) are loaded again.
  const restoringDetails = useRef(selectedGameKey !== null);

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
        if (!cancelled) setPluginUsageInfo(`Disk usage unavailable: ${errorText(error)}`);
      });
    return () => {
      cancelled = true;
    };
  }, [dataRevision, cachedScanTime, clockTick]);

  useEffect(() => {
    const timer = window.setInterval(() => setClockTick((tick) => tick + 1), 60_000);
    return () => window.clearInterval(timer);
  }, []);

  // Scans run only on request: a catalog or settings change just says the list may be out of date.
  useEffect(() => {
    if (dataRevision === initialRevision.current) return;
    initialRevision.current = dataRevision;
    setScanOutdated(true);
  }, [dataRevision]);

  // While scanning, show the plugin log lines written since "Scan started" (slow SD cards and USB drives).
  useEffect(() => {
    if (!scanning) {
      setScanLog("");
      return;
    }
    const timer = window.setInterval(() => {
      getLog(200).then(({ content }) => {
        const lines = content.split("\n");
        const start = lines.map((line) => line.endsWith(" Scan started")).lastIndexOf(true);
        const recent = start < 0 ? [] : lines.slice(start + 1).slice(-8);
        setScanLog(recent.map((line) => line.replace(/^\S+ \S+ \w+ /, "")).join("\n")); // Drop date, time, level.
      }, () => undefined);
    }, 1000);
    return () => window.clearInterval(timer);
  }, [scanning]);

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
      setScanOutdated(false);
    } catch (error) {
      notifyError("Steam scan failed", `Supported game scan failed: ${errorText(error)}`);
    } finally {
      setScanning(false);
    }
  };

  /** Rescan to refresh the game list; returns the error text instead of throwing. */
  const refresh = async (): Promise<string | null> => {
    try {
      setReport(await scanSupportedGames());
      setCachedScanTime(new Date().toISOString());
      setScanOutdated(false);
      return null;
    } catch (error) {
      return errorText(error);
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
    const step = (text: string) => setModProgress({ key: gameKey(game), step: text });
    const refreshStatus = () => {
      step("Refreshing status");
      return refresh();
    };
    const filesStep = applied ? "Copying mod files" : "Removing mod files";
    step(applied && hasOverrides ? "Reading launch options" : filesStep);
    setErrorMessage("");
    try {
      if (applied && game.offline_only && !await confirmOfflineMod(game.app_name)) return;
      const launch = applied && hasOverrides ? await planLaunchOptions(game) : { plan: null, notice: "" };

      let outcome: Awaited<ReturnType<typeof setModFiles>>;
      try {
        step(filesStep);
        outcome = await setModFiles(target, applied);
      } catch (error) {
        const refreshError = await refreshStatus();
        if (!applied && hasOverrides) {
          await showRemovalFailure(game, error, refreshError);
        } else {
          const action = applied ? "apply" : "remove";
          notifyError(
            `${applied ? "Apply" : "Remove"} failed`,
            withRefreshError(`Could not ${action} ${game.mod_name}: ${errorText(error)}`, refreshError),
          );
        }
        return;
      }
      if (outcome.warning) {
        showErrorModal("Mod removal needs attention", outcome.warning);
        await refreshStatus();
        return;
      }
      if (outcome.conflicts.length === 0) {
        if (applied) markUpToDate(game);
        else forgetChoice(game);
      }
      if (outcome.iniWarning) setErrorMessage(outcome.iniWarning);

      if (!applied) {
        if (hasOverrides) step("Removing launch options");
        const refreshError = hasOverrides ? await removeModLaunchOptions(game, refreshStatus) : await refreshStatus();
        if (refreshError && hasOverrides) setErrorMessage(`Status refresh failed: ${refreshError}`);
        else if (refreshError) {
          notifyError("Status refresh failed", `Operation completed, but refreshing status failed: ${refreshError}`);
        }
        return;
      }
      let notice = launch.notice;
      if (launch.plan) {
        try {
          step("Setting launch options");
          await applyLaunchOptions(launch.plan, game.wine_dll_overrides);
        } catch (error) {
          notice = `Steam launch options could not be updated: ${errorText(error)}. Mod files were still applied.`;
        }
      }
      const refreshError = await refreshStatus();
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
      setModProgress(null);
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
  // Cards keep their position in the full list as their number, so a filtered card reads the same.
  const shownGroups = gameGroups
    .map((group, position) => ({ group, position }))
    .filter(({ group }) => group.some((game) => gameMatchesFilter(game, filter)));

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
        notifyError("Could not load ReadMe", errorText(error));
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
        return { name, error: errorText(error) };
      }
    }));
    if (requestId === detailRequestId.current) setModLogs(results);
  };

  useEffect(() => {
    if (!restoringDetails.current || !selectedGame) return;
    restoringDetails.current = false;
    if (detailView === "readme") void openGameReadme(selectedGame);
    else if (detailView === "log") void openGameLog(selectedGame);
    else void openGameDetails(selectedGame);
  }, [selectedGame]);

  const resetGameChoice = async (game: SupportedGame) => {
    const group = game.ambiguity_group;
    if (!group || !game.game_path) return;
    setModProgress({ key: gameKey(game), step: "Clearing game choice" });
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
      notifyError("Could not clear game choice", errorText(error));
    } finally {
      setModProgress(null);
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
      {!selectedGame && scanLog && (
        <PanelSectionRow>
          <div style={{ fontSize: "12px", opacity: 0.8, width: "100%" }}><LogText text={scanLog} /></div>
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
      {!selectedGame && report && scanOutdated && (
        <PanelSectionRow>
          <div role="status">The catalog or settings changed. Press Scan to refresh this list.</div>
        </PanelSectionRow>
      )}
      {!selectedGame && pluginUsageInfo && (
        <PanelSectionRow>
          <div role="status">{pluginUsageInfo}</div>
        </PanelSectionRow>
      )}
      {!selectedGame && gameGroups.length > 0 && (
        <PanelSectionRow>
          <GameFilter value={filter} onChange={setFilter} />
        </PanelSectionRow>
      )}
      {!selectedGame && filter.trim() && (
        <PanelSectionRow>
          <div role="status">
            {shownGroups.length
              ? `Showing ${shownGroups.length} of ${gameGroups.length} games.`
              : `No games match "${filter.trim()}".`}
          </div>
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
          onIniCommit={ini.commit}
        />
      ) : shownGroups.map(({ group, position: index }, shownIndex, ordered) => {

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
            notifyError("Game selection failed", errorText(error));
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
            {first.manual && !ordered[shownIndex - 1]?.group[0].manual && (
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
              progress={game && modProgress?.key === gameKey(game) ? modProgress.step : null}
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
