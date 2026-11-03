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
import {
  applyLaunchOptions,
  prepareLaunchOptions,
  readCurrentLaunchOptions,
  removeLaunchOptions,
} from "./launch_options/steamLaunchOptions";

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
    wine_dll_overrides: string[];
  }[];
  warnings: string[];
};

const scanSupportedGames = callable<[], SupportedGameReport>("scan_supported_games");
const setModApplied = callable<
  [string, string, boolean, boolean | null, boolean | null],
  {
    applied: boolean;
    copied_file_count: number;
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

function confirmModifiedIniRemoval(
  gameName: string,
  iniFile: string,
): Promise<boolean> {
  return new Promise((resolve) => {
    let settled = false;
    let closeModal: (() => void) | undefined;
    const finish = (keepIni: boolean) => {
      if (settled) return;
      settled = true;
      resolve(keepIni);
      closeModal?.();
    };

    const modal = showModal(
      <ConfirmModal
        strTitle="Keep modified configuration?"
        strDescription={`${gameName}'s ${iniFile} differs from the mod's original configuration. Keep it when removing the mod files?`}
        strOKButtonText="Keep configuration"
        strCancelButtonText="Delete configuration"
        onOK={() => finish(true)}
        onCancel={() => finish(false)}
        onEscKeypress={() => finish(true)}
      />,
      undefined,
      { fnOnClose: () => finish(true) },
    );
    closeModal = modal.Close;
  });
}

function confirmLaunchOptionsChange(
  gameName: string,
  requiredGroups: string[],
  currentOptions: string,
  proposedOptions: string,
): Promise<boolean> {
  return new Promise((resolve) => {
    let settled = false;
    let closeModal: (() => void) | undefined;
    const finish = (confirmed: boolean) => {
      if (settled) return;
      settled = true;
      resolve(confirmed);
      closeModal?.();
    };
    const modal = showModal(
      <ConfirmModal
        strTitle="Update Steam launch options"
        strDescription={`${gameName} requires WINEDLLOVERRIDES="${requiredGroups.join(";")}".\n\nCurrent launch options:\n${currentOptions || "(empty)"}\n\nUpdated launch options:\n${proposedOptions || "(empty)"}\n\nContinue and apply this mod?`}
        strOKButtonText="Update options"
        strCancelButtonText="Skip launch options"
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

type LaunchOptionsDisplay = {
  label: string;
  value: string;
};

async function copyToClipboard(value: string): Promise<void> {
  try {
    if (!navigator.clipboard?.writeText) {
      throw new Error("Clipboard API is unavailable.");
    }
    await navigator.clipboard.writeText(value);
    return;
  } catch {
    const textarea = document.createElement("textarea");
    textarea.value = value;
    textarea.setAttribute("readonly", "");
    textarea.style.position = "fixed";
    textarea.style.opacity = "0";
    document.body.appendChild(textarea);
    let copied = false;
    try {
      textarea.select();
      copied = document.execCommand("copy");
    } finally {
      textarea.remove();
    }
    if (!copied) throw new Error("Could not copy to the clipboard.");
  }
}

function LaunchOptionsInfoModal({
  title,
  message,
  values,
  closeModal,
}: {
  title: string;
  message: string;
  values: LaunchOptionsDisplay[];
  closeModal: () => void;
}) {
  const [copyStatus, setCopyStatus] = useState("");

  return (
    <ConfirmModal
      bAlertDialog
      bAllowFullSize
      strTitle={title}
      strDescription={message}
      strOKButtonText="Close"
      onOK={closeModal}
      onCancel={closeModal}
    >
      {values.map(({ label, value }) => (
        <div key={label} style={{ marginTop: "12px" }}>
          <strong>{label}</strong>
          <div
            style={{
              maxHeight: "18vh",
              overflowY: "auto",
              overflowWrap: "anywhere",
              whiteSpace: "pre-wrap",
              margin: "6px 0",
            }}
          >
            {value || "(empty)"}
          </div>
          <DialogButton
            onClick={async () => {
              try {
                await copyToClipboard(value);
                setCopyStatus(`${label} copied.`);
              } catch (error) {
                setCopyStatus(`Copy failed: ${String(error)}`);
              }
            }}
          >
            Copy
          </DialogButton>
        </div>
      ))}
      {copyStatus && (
        <div role="status" style={{ marginTop: "8px" }}>
          {copyStatus}
        </div>
      )}
    </ConfirmModal>
  );
}

function showLaunchOptionsInfo(
  title: string,
  message: string,
  values: LaunchOptionsDisplay[],
): void {
  let closeModal = () => {};
  const modal = showModal(
    <LaunchOptionsInfoModal
      title={title}
      message={message}
      values={values}
      closeModal={() => closeModal()}
    />,
  );
  closeModal = modal.Close;
}

function isEditableIniLine(line: string): boolean {
  const trimmed = line.trim();
  return (
    trimmed.length > 0 &&
    !/^\[[^\]]*\]\s*(?:[;#].*)?$/.test(trimmed) &&
    !/^[;#]/.test(trimmed)
  );
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
  const iniSavingRef = useRef(false);
  const pendingIniSave = useRef(false);
  const saveIniRef = useRef<() => Promise<void>>(async () => {});

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
        } else if (
          launchPlan &&
          launchPlan.proposedOptions !== launchPlan.currentOptions &&
          !(await confirmLaunchOptionsChange(
            game.app_name,
            game.wine_dll_overrides,
            launchPlan.currentOptions,
            launchPlan.proposedOptions,
          ))
        ) {
          launchPlan = null;
          manualLaunchOptions =
            `WINEDLLOVERRIDES="${game.wine_dll_overrides.join(";")}" %command%`;
          launchOptionsNotice =
            `Steam launch options were left unchanged. To enable the mod's Wine DLL overrides, add ${manualLaunchOptions} to ${game.app_name}'s Steam launch options.`;
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
        if (result.conflicting_files.length > 0) {
          if (applied) {
            const replaceFiles = await confirmFileReplacement(
              game.app_name,
              result.conflicting_files,
            );
            await setModApplied(
              game.mod_id,
              game.game_path,
              true,
              replaceFiles,
              null,
            );
          } else {
            const keepIni = await confirmModifiedIniRemoval(
              game.app_name,
              result.conflicting_files[0],
            );
            await setModApplied(
              game.mod_id,
              game.game_path,
              false,
              null,
              keepIni,
            );
          }
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
    `${game.steam_app_id}-${game.game_path}`;
  const selectedGame = report?.supported_games.find(
    (game) => gameKey(game) === selectedGameKey,
  );

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
                iniSavingRef.current = false;
                pendingIniSave.current = false;
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
              {selectedGame.mod_files.map((file) => (
                <PanelSectionRow key={file}>
                  <div style={{ overflowWrap: "anywhere" }}>{file}</div>
                </PanelSectionRow>
              ))}
              {selectedGame.ini_file ? (
                <>
                  <PanelSectionRow>
                    <strong>Configuration: {selectedGame.ini_file}</strong>
                  </PanelSectionRow>
                  <PanelSectionRow>
                    <div>
                      {iniLoaded
                        ? iniExists
                          ? iniSaving
                            ? "Saving configuration..."
                            : "Changes save automatically when a setting field loses focus. Section headers, blank lines, and comments are shown but cannot be edited."
                          : "INI file is not installed yet."
                        : "Loading INI configuration..."}
                    </div>
                  </PanelSectionRow>
                  {iniLines.map((line, index) => (
                    <PanelSectionRow key={`${selectedGame.mod_id}-ini-line-${index}`}>
                      {isEditableIniLine(line) ? (
                        <TextField
                          value={line}
                          disabled={!iniLoaded}
                          onBlur={() => {
                            if (!iniLoaded) return;
                            if (iniSavingRef.current) {
                              pendingIniSave.current = true;
                            } else if (iniLines.join("\n") !== savedIniContent) {
                              void saveIni();
                            }
                          }}
                          onChange={(event) => {
                            const updatedLines = [...iniLines];
                            updatedLines[index] = event.target.value;
                            setIniLines(updatedLines);
                            setIniStatus("");
                          }}
                        />
                      ) : (
                        <div
                          style={{
                            minHeight: "1em",
                            overflowWrap: "anywhere",
                            whiteSpace: "pre-wrap",
                          }}
                        >
                          {line}
                        </div>
                      )}
                    </PanelSectionRow>
                  ))}
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

export default definePlugin(() => ({
  name: "AnyAspectRatio",
  titleView: (
    <div className={staticClasses.Title}>AnyAspectRatio</div>
  ),
  content: <Content />,
  icon: <span aria-hidden="true">AR</span>,
}));
