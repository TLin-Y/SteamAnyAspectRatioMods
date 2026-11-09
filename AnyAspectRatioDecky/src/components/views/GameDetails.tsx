import { ButtonItem, PanelSectionRow, TextField } from "@decky/ui";
import ReactMarkdown from "react-markdown";
import { isEditableIniLine } from "../../services/modals";
import type { ModLogResult, SupportedGame } from "../../services/types";

export function GameTitle({ game, index, unresolved = false }: {
  game: SupportedGame; index?: number; unresolved?: boolean;
}) {
  const color = unresolved ? "#9e9e9e" : game.manual ? "#ff9830" : game.matched_by_executable ? "#9e9e9e" : "#64b5f6";
  return (
    <strong style={{ display: "block", overflowWrap: "anywhere", color }}>
      {index !== undefined && `${index + 1}. `}
      {game.app_name}
      {[...(game.display_tags ?? []), ...(game.manual ? ["manual"] : [])].map((tag) =>
        ` (${tag.replace(/[-_]/g, " ").replace(/\b\w/g, (letter) => letter.toUpperCase())})`,
      )}
      {game.offline_only && " (Offline)"}
      {game.source_kind === "local_archive" && " (Custom)"}
      {game.update_available && " (New)"}
      {(unresolved || game.matched_by_executable) && " (.exe)"}
    </strong>
  );
}

type Props = {
  game: SupportedGame;
  view: "manage" | "readme" | "log";
  readme: { title: string; content: string; loading: boolean };
  logs: ModLogResult[] | null;
  ini: { lines: string[]; loaded: boolean; exists: boolean; saving: boolean; status: string };
  onBack: () => void;
  onIniBlur: () => void;
  onIniChange: (index: number, value: string) => void;
};

export function GameDetails({ game, view, readme, logs, ini, onBack, onIniBlur, onIniChange }: Props) {
  return (
    <>
      <PanelSectionRow>
        <ButtonItem layout="below" onClick={onBack}>Back to games</ButtonItem>
      </PanelSectionRow>
      <PanelSectionRow><GameTitle game={game} /></PanelSectionRow>
      {view === "readme" ? (
        <PanelSectionRow>
          <div style={{ maxHeight: "60vh", overflowY: "auto", overflowWrap: "anywhere" }}>
            {readme.loading ? "Loading ReadMe..." : (
              <>
                <div style={{ opacity: 0.7, fontSize: "0.85em" }}>
                  Game folder: {game.game_path ?? "unknown"}
                </div>
                <strong>{readme.title}</strong>
                <ReactMarkdown>{readme.content}</ReactMarkdown>
              </>
            )}
          </div>
        </PanelSectionRow>
      ) : view === "log" ? (
        <PanelSectionRow>
          <div style={{ maxHeight: "60vh", overflowY: "auto", overflowWrap: "anywhere", width: "100%" }}>
            {logs ? logs.map((entry) => (
              <div key={entry.name}>
                <strong>{entry.name}</strong>
                {"error" in entry ? (
                  <div role="status">Could not load log: {entry.error}</div>
                ) : (
                  <>
                    {entry.truncated && <div>Showing the last 256 KiB of this log.</div>}
                    <pre style={{ whiteSpace: "pre-wrap", overflowWrap: "anywhere" }}>{entry.content}</pre>
                  </>
                )}
              </div>
            )) : "Loading mod log..."}
          </div>
        </PanelSectionRow>
      ) : (
        <>
          {game.mod_files.map((file) => (
            <PanelSectionRow key={file}><div style={{ overflowWrap: "anywhere" }}>{file}</div></PanelSectionRow>
          ))}
          {game.ini_file ? (
            <>
              <PanelSectionRow><strong>Configuration: {game.ini_file}</strong></PanelSectionRow>
              <PanelSectionRow>
                <div>
                  {!ini.loaded ? "Loading INI configuration..." : !ini.exists ? "INI file is not installed yet." :
                    ini.saving ? "Saving configuration..." :
                    "Changes save automatically when a setting field loses focus. Section headers, blank lines, and comments are shown but cannot be edited."}
                </div>
              </PanelSectionRow>
              {ini.lines.map((line, index) => (
                <PanelSectionRow key={`${game.mod_id}-ini-line-${index}`}>
                  {isEditableIniLine(line) ? (
                    <TextField
                      value={line}
                      disabled={!ini.loaded}
                      onBlur={onIniBlur}
                      onChange={(event) => onIniChange(index, event.target.value)}
                    />
                  ) : (
                    <div style={{ minHeight: "1em", overflowWrap: "anywhere", whiteSpace: "pre-wrap" }}>
                      {line}
                    </div>
                  )}
                </PanelSectionRow>
              ))}
              {ini.status && <PanelSectionRow><div role="status">{ini.status}</div></PanelSectionRow>}
            </>
          ) : (
            <PanelSectionRow><div>This mod does not define an INI configuration file.</div></PanelSectionRow>
          )}
        </>
      )}
    </>
  );
}
