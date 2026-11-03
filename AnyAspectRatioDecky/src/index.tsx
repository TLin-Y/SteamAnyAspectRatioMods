import {
  ButtonItem,
  ConfirmModal,
  DialogButton,
  PanelSection,
  PanelSectionRow,
  staticClasses,
  TextField,
  ToggleField,
  showModal,
} from "@decky/ui";
import { callable, definePlugin } from "@decky/api";
import ReactMarkdown from "react-markdown";
import { useRef, useState } from "react";

type SupportedGameReport = {
  steam_scan: {
    libraries_scanned: number;
    apps_scanned: number;
  };
  catalogued_mods: number;
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
  }[];
  warnings: string[];
};

const scanSupportedGames = callable<[], SupportedGameReport>("scan_supported_games");
const setModApplied = callable<
  [string, string, boolean, boolean | null],
  {
    applied: boolean;
    file_count: number;
    conflicting_files: string[];
  }
>("set_mod_applied");
const getModIni = callable<
  [string, string],
  { ini_file: string; content: string; exists: boolean }
>("get_mod_ini");
const saveModIni = callable<
  [string, string, string],
  { saved: boolean; ini_file: string | null }
>("save_mod_ini");
const getModReadme = callable<[string], { title: string; content: string }>(
  "get_mod_readme",
);

function confirmFileReplacement(
  gameName: string,
  conflictingFiles: string[],
): Promise<boolean> {
  return new Promise((resolve) => {
    let settled = false;
    let closeModal: (() => void) | undefined;
    const finish = (replaceIni: boolean) => {
      if (settled) return;
      settled = true;
      resolve(replaceIni);
      closeModal?.();
    };

    const modal = showModal(
      <ConfirmModal
        strTitle="Existing mod files differ"
        strDescription={`${gameName} has files that differ from this mod's source:\n${conflictingFiles.join("\n")}\n\nReplace these files? Choosing “Keep existing files” preserves them and still applies the other mod files.`}
        strOKButtonText="Replace files"
        strCancelButtonText="Keep existing files"
        onOK={() => finish(true)}
        onCancel={() => finish(false)}
        onEscKeypress={() => finish(false)}
      />,
      undefined,
      { fnOnClose: () => finish(false) },
    );
    closeModal = modal.Close;
  });
}

function Content() {
  const [report, setReport] = useState<SupportedGameReport | null>(null);
  const [errorMessage, setErrorMessage] = useState("");
  const [scanning, setScanning] = useState(false);
  const [updatingMod, setUpdatingMod] = useState(false);
  const [toggleRevision, setToggleRevision] = useState(0);
  const [selectedGameKey, setSelectedGameKey] = useState<string | null>(null);
  const [iniGameKey, setIniGameKey] = useState<string | null>(null);
  const [detailView, setDetailView] = useState<"manage" | "readme">("manage");
  const [readmeTitle, setReadmeTitle] = useState("");
  const [readmeContent, setReadmeContent] = useState("");
  const [readmeLoading, setReadmeLoading] = useState(false);
  const [iniLines, setIniLines] = useState<string[]>([]);
  const [savedIniContent, setSavedIniContent] = useState("");
  const [iniExists, setIniExists] = useState(false);
  const [iniLoaded, setIniLoaded] = useState(false);
  const [iniSaving, setIniSaving] = useState(false);
  const [iniStatus, setIniStatus] = useState("");
  const detailRequestId = useRef(0);

  const notifyError = (title: string, message: string) => {
    setErrorMessage(message);
    showModal(
      <ConfirmModal
        bAlertDialog
        strTitle={title}
        strDescription={message}
        strOKButtonText="OK"
      />,
    );
  };

  const scanForSupportedGames = async () => {
    setScanning(true);
    setReport(null);
    setErrorMessage("");
    try {
      setReport(await scanSupportedGames());
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
        return null;
      } catch (error) {
        return String(error);
      }
    };

    try {
      try {
        const result = await setModApplied(
          game.mod_id,
          game.game_path,
          applied,
          null,
        );
        if (result.conflicting_files.length > 0) {
          const replaceFiles = await confirmFileReplacement(
            game.app_name,
            result.conflicting_files,
          );
          await setModApplied(
            game.mod_id,
            game.game_path,
            true,
            replaceFiles,
          );
        }
      } catch (error) {
        const refreshError = await refreshReport();
        notifyError(
          `${applied ? "Apply" : "Remove"} failed`,
          `Could not ${applied ? "apply" : "remove"} ${game.mod_name}: ${String(error)}` +
            (refreshError ? ` Status refresh failed: ${refreshError}` : ""),
        );
        return;
      }

      const refreshError = await refreshReport();
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
    `${game.steam_app_id}-${game.game_path}`;
  const selectedGame = report?.supported_games.find(
    (game) => gameKey(game) === selectedGameKey,
  );

  const openGameDetails = async (
    game: SupportedGameReport["supported_games"][number],
  ) => {
    const requestId = ++detailRequestId.current;
    const selectedKey = gameKey(game);
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

  const saveIni = async () => {
    const gameBeingSaved = selectedGame;
    if (
      !gameBeingSaved?.game_path ||
      !gameBeingSaved.ini_file ||
      iniGameKey !== gameKey(gameBeingSaved)
    ) return;
    const requestId = detailRequestId.current;
    const gamePath = gameBeingSaved.game_path;
    setIniSaving(true);
    setIniStatus("");
    const iniContent = iniLines.join("\n");
    try {
      await saveModIni(
        gameBeingSaved.mod_id,
        gamePath,
        iniContent,
      );
      if (requestId !== detailRequestId.current) return;
      setSavedIniContent(iniContent);
      setIniExists(true);
      setIniStatus("Configuration saved.");
    } catch (error) {
      if (requestId === detailRequestId.current) {
        notifyError("Could not save configuration", String(error));
      }
      return;
    } finally {
      if (requestId === detailRequestId.current) {
        setIniSaving(false);
      }
    }

    try {
      setReport(await scanSupportedGames());
    } catch (error) {
      if (requestId === detailRequestId.current) {
        notifyError(
          "Configuration saved, but status refresh failed",
          String(error),
        );
      }
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
                ? `Scanned ${report.steam_scan.apps_scanned} installed Steam apps across ${report.steam_scan.libraries_scanned} libraries. Found ${report.supported_games.length} supported installation(s); ${report.catalogued_mods} fix(es) are in the catalog.`
                : "Steam libraries have not been scanned.")}
          </div>
        </PanelSectionRow>
      )}
      {report?.supported_games.length === 0 && (
        <PanelSectionRow>
          <div role="status">No catalogued games were found in the scanned Steam libraries.</div>
        </PanelSectionRow>
      )}
      {selectedGame ? (
        <>
          <PanelSectionRow>
            <ButtonItem
              layout="below"
              onClick={() => {
                detailRequestId.current += 1;
                setSelectedGameKey(null);
                setDetailView("manage");
                setIniGameKey(null);
                setIniSaving(false);
                setReadmeLoading(false);
              }}
            >
              Back to games
            </ButtonItem>
          </PanelSectionRow>
          <PanelSectionRow>
            <strong style={{ overflowWrap: "anywhere" }}>{selectedGame.app_name}</strong>
          </PanelSectionRow>
          {detailView === "readme" ? (
            <PanelSectionRow>
              <div
                style={{
                  maxHeight: "60vh",
                  overflowY: "auto",
                  overflowWrap: "anywhere",
                }}
              >
                {readmeLoading ? (
                  "Loading ReadMe..."
                ) : (
                  <>
                    <strong>{readmeTitle}</strong>
                    <ReactMarkdown>{readmeContent}</ReactMarkdown>
                  </>
                )}
              </div>
            </PanelSectionRow>
          ) : (
            <>
              <PanelSectionRow>
                <div>Installed files ({selectedGame.files_present.length}/{selectedGame.mod_files.length})</div>
              </PanelSectionRow>
              {selectedGame.mod_files.map((file) => {
                const present = selectedGame.files_present.includes(file);
                return (
                  <PanelSectionRow key={file}>
                    <div style={{ overflowWrap: "anywhere" }}>
                      {present ? "Present" : "Missing"} · {file}
                    </div>
                  </PanelSectionRow>
                );
              })}
              {selectedGame.ini_file ? (
                <>
                  <PanelSectionRow>
                    <strong>Configuration: {selectedGame.ini_file}</strong>
                  </PanelSectionRow>
                  <PanelSectionRow>
                    <div>
                      {iniLoaded
                        ? iniExists
                          ? "Select a line to edit it with the Steam on-screen keyboard."
                          : "INI file is not installed yet. Add lines below to create it."
                        : "Loading INI configuration..."}
                    </div>
                  </PanelSectionRow>
                  {iniLines.map((line, index) => (
                    <PanelSectionRow key={`${selectedGame.mod_id}-ini-line-${index}`}>
                      <TextField
                        label={`Line ${index + 1}`}
                        value={line}
                        disabled={!iniLoaded || iniSaving}
                        onChange={(event) => {
                          const updatedLines = [...iniLines];
                          updatedLines[index] = event.target.value;
                          setIniLines(updatedLines);
                          setIniStatus("");
                        }}
                      />
                    </PanelSectionRow>
                  ))}
                  <PanelSectionRow>
                    <ButtonItem
                      layout="below"
                      disabled={!iniLoaded || iniSaving}
                      onClick={() => {
                        setIniLines([...iniLines, ""]);
                        setIniStatus("");
                      }}
                    >
                      Add line
                    </ButtonItem>
                  </PanelSectionRow>
                  {iniLines.length > 1 && (
                    <PanelSectionRow>
                      <ButtonItem
                        layout="below"
                        disabled={!iniLoaded || iniSaving}
                        onClick={() => {
                          setIniLines(iniLines.slice(0, -1));
                          setIniStatus("");
                        }}
                      >
                        Remove last line
                      </ButtonItem>
                    </PanelSectionRow>
                  )}
                  <PanelSectionRow>
                    <ButtonItem
                      layout="below"
                      disabled={
                        !iniLoaded ||
                        iniSaving ||
                        iniLines.join("\n") === savedIniContent
                      }
                      onClick={saveIni}
                    >
                      {iniSaving ? "Saving configuration..." : "Save configuration"}
                    </ButtonItem>
                  </PanelSectionRow>
                  {iniStatus && (
                    <PanelSectionRow>
                      <div role="status">{iniStatus}</div>
                    </PanelSectionRow>
                  )}
                </>
              ) : (
                <PanelSectionRow>
                  <div>This mod does not define an INI configuration file.</div>
                </PanelSectionRow>
              )}
            </>
          )}
        </>
      ) : report?.supported_games.map((game, index) => (
        <PanelSection key={`${game.steam_app_id}-${game.game_path}`}>
          <PanelSectionRow>
            <strong style={{ overflowWrap: "anywhere" }}>
              {index + 1}. {game.app_name}
            </strong>
          </PanelSectionRow>
          <PanelSectionRow>
            <div
              style={{
                display: "flex",
                gap: "8px",
                justifyContent: "center",
                width: "100%",
              }}
            >
              <DialogButton
                style={{ flex: "0 0 30%" }}
                onClick={() => openGameDetails(game)}
                disabled={updatingMod || !game.game_path}
              >
                Edit
              </DialogButton>
              <DialogButton
                style={{ flex: "0 0 30%" }}
                onClick={() => openGameReadme(game)}
                disabled={readmeLoading}
              >
                ReadMe
              </DialogButton>
            </div>
          </PanelSectionRow>
          <PanelSectionRow>
            <ToggleField
              key={`${game.steam_app_id}-${toggleRevision}`}
              label="Apply"
              checked={game.file_status === "complete"}
              disabled={updatingMod || !game.game_path}
              onChange={(checked) => changeModApplied(game, checked)}
            />
          </PanelSectionRow>
          {game.file_status === "partial" && (
            <PanelSectionRow>
              <ButtonItem
                layout="below"
                disabled={updatingMod || !game.game_path}
                onClick={() => changeModApplied(game, false)}
              >
                Remove partial mod files
              </ButtonItem>
            </PanelSectionRow>
          )}
          <PanelSectionRow>
            <div>
              {game.file_status === "complete"
                ? `Applied ${game.files_present.length} mod files`
                : game.file_status === "partial"
                  ? `Not applied — ${game.files_present.length}/${
                      game.files_present.length + game.files_missing.length
                    } mod files present`
                  : "Not applied — no mod files present"}
            </div>
          </PanelSectionRow>
        </PanelSection>
      ))}
      {!selectedGame && report?.warnings.map((warning, index) => (
        <PanelSectionRow key={`${index}-${warning}`}>
          <div role="status">{warning}</div>
        </PanelSectionRow>
      ))}
    </PanelSection>
  );
}

export default definePlugin(() => ({
  name: "AnyAspectRatio",
  titleView: (
    <div className={staticClasses.Title}>AnyAspectRatio</div>
  ),
  content: <Content />,
  icon: <span aria-hidden="true">AR</span>,
}));
