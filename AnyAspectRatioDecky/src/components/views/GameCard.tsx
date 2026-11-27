import { ButtonItem, DialogButton, Navigation, PanelSection, PanelSectionRow, ToggleField } from "@decky/ui";
import { MOD_LOG_BUTTON_STYLE } from "../../services/formatters";
import { openAuthorPage } from "../../services/modals";
import { DESKTOP_ACTIONS } from "../../services/platform";
import type { SupportedGame } from "../../services/types";
import { GameTitle } from "./GameDetails";

const DESKTOP_BUTTON_STYLE = { flex: "1 1 0", minWidth: 0, height: "36px", padding: "4px" };
const DECK_BUTTON_STYLE = { flex: "0 0 30%", height: "36px", padding: "4px" };
const ACTION_BUTTON_STYLE = DESKTOP_ACTIONS ? DESKTOP_BUTTON_STYLE : DECK_BUTTON_STYLE;
const fileName = (path: string) => path.split(/[\\/]/).pop() ?? path;

type Props = {
  first: SupportedGame;
  game: SupportedGame | undefined;
  index: number;
  groupId: string | null | undefined;
  toggleRevision: number;
  updating: boolean;
  /** The step this game's mod change is on, while it runs. */
  progress: string | null;
  readmeLoading: boolean;
  onSelect: () => void;
  onApply: (game: SupportedGame, applied: boolean) => void;
  onClear: (game: SupportedGame) => void;
  onEdit: (game: SupportedGame) => void;
  onReadme: (game: SupportedGame) => void;
  onLog: (game: SupportedGame) => void;
  onChooseDownload: (game: SupportedGame) => void;
};

export function GameCard({
  first, game, index, groupId, toggleRevision, updating, progress, readmeLoading,
  onSelect, onApply, onClear, onEdit, onReadme, onLog, onChooseDownload,
}: Props) {
  if (!game) {
    return (
      <PanelSection>
        <PanelSectionRow>
          <GameTitle game={first} index={index} unresolved />
        </PanelSectionRow>
        <PanelSectionRow>
          <ToggleField
            key={`${groupId}-${toggleRevision}`}
            label="Apply"
            checked={false}
            disabled={updating}
            onChange={(checked) => { if (checked) onSelect(); }}
          />
        </PanelSectionRow>
      </PanelSection>
    );
  }

  const authorPage = game.download_page;
  const needsZip = !!authorPage && !game.download_file && game.file_status !== "complete";
  const toggle = (
    <ToggleField
      key={`${game.steam_app_id}-${toggleRevision}`}
      label={game.file_status === "partial" ? "Apply *" : "Apply"}
      checked={game.file_status === "complete"}
      disabled={updating || !game.game_path || needsZip}
      onChange={(checked) => onApply(game, checked)}
    />
  );
  const log = game.log_files?.length ? (
    <DialogButton style={MOD_LOG_BUTTON_STYLE} disabled={updating} onClick={() => onLog(game)}>
      Log
    </DialogButton>
  ) : null;

  return (
    <PanelSection>
      <PanelSectionRow><GameTitle game={game} index={index} /></PanelSectionRow>
      {groupId && <PanelSectionRow><div role="status">Selected fix: {game.mod_name}</div></PanelSectionRow>}
      {groupId && (game.manual || game.file_status === "none") && (
        <PanelSectionRow>
          <ButtonItem layout="below" disabled={updating} onClick={() => onClear(game)}>
            Clear game choice
          </ButtonItem>
        </PanelSectionRow>
      )}
      <PanelSectionRow>
        <div style={{ display: "flex", gap: "8px", justifyContent: "center", width: "100%" }}>
          {DESKTOP_ACTIONS && (
            <DialogButton
              style={DESKTOP_BUTTON_STYLE}
              onClick={() => DESKTOP_ACTIONS?.openFolder(game)}
              disabled={!game.game_path}
            >
              Open
            </DialogButton>
          )}
          {needsZip ? (
            <>
              <DialogButton style={ACTION_BUTTON_STYLE} onClick={() => openAuthorPage(authorPage)}>
                Get
              </DialogButton>
              <DialogButton
                style={ACTION_BUTTON_STYLE}
                disabled={updating}
                onClick={() => onChooseDownload(game)}
              >
                Choose zip
              </DialogButton>
            </>
          ) : (
            <>
              {!game.manual && (
                <DialogButton
                  style={ACTION_BUTTON_STYLE}
                  onClick={() => onEdit(game)}
                  disabled={updating || !game.game_path}
                >
                  Edit
                </DialogButton>
              )}
              <DialogButton
                style={game.manual && !DESKTOP_ACTIONS ? { ...DECK_BUTTON_STYLE, flex: "0 0 60%" } : ACTION_BUTTON_STYLE}
                onClick={() => authorPage ? Navigation.NavigateToExternalWeb(authorPage) : onReadme(game)}
                disabled={readmeLoading}
              >
                ReadMe
              </DialogButton>
            </>
          )}
          {DESKTOP_ACTIONS && game.wine_dll_overrides.length > 0 && (
            <DialogButton
              style={DESKTOP_BUTTON_STYLE}
              onClick={() => DESKTOP_ACTIONS?.showWineOverrides(game)}
              disabled={!game.game_path}
            >
              Wine
            </DialogButton>
          )}
        </div>
      </PanelSectionRow>
      {game.manual ? (
        <PanelSectionRow>
          {log ? (
            <div style={{ display: "flex", alignItems: "center", gap: "8px", width: "100%" }}>
              <div style={{ flex: 1 }}>Manual install — see the ReadMe</div>
              {log}
            </div>
          ) : <div>Manual install — see the ReadMe</div>}
        </PanelSectionRow>
      ) : (
        <>
          {(needsZip || game.download_file) && (
            <PanelSectionRow>
              <div role="status" style={{ overflowWrap: "anywhere" }}>
                {needsZip
                  ? "Not hosted here. Get the zip from the author's page, then Scan."
                  : `Zip: ${fileName(game.download_file ?? "")}`}
              </div>
            </PanelSectionRow>
          )}
          <PanelSectionRow>
            {log ? (
              <div style={{ display: "flex", alignItems: "center", gap: "16px", width: "100%" }}>
                <div style={{ flex: 1, minWidth: 0 }}>{toggle}</div>
                {log}
              </div>
            ) : toggle}
          </PanelSectionRow>
          {game.file_status === "partial" && (
            <PanelSectionRow>
              <ButtonItem layout="below" disabled={updating || !game.game_path} onClick={() => onApply(game, false)}>
                Remove partial mod files
              </ButtonItem>
            </PanelSectionRow>
          )}
          <PanelSectionRow>
            <div role="status" style={progress ? { color: "#ffd84d" } : undefined}>
              {progress
                ? `${progress}...`
                : game.file_status === "complete"
                ? `Applied ${game.files_present.length} mod files`
                : game.file_status === "partial"
                  ? `* Incomplete — ${game.files_present.length}/${game.files_present.length + game.files_missing.length} mod files present`
                  : "Not applied — no mod files present"}
            </div>
          </PanelSectionRow>
        </>
      )}
    </PanelSection>
  );
}
