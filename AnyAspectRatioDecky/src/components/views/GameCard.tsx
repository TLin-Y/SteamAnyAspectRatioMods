import { ButtonItem, DialogButton, PanelSection, PanelSectionRow, ToggleField } from "@decky/ui";
import { MOD_LOG_BUTTON_STYLE } from "../../services/formatters";
import type { SupportedGame } from "../../services/types";
import { GameTitle } from "./GameDetails";

type Props = {
  first: SupportedGame;
  game: SupportedGame | undefined;
  index: number;
  groupId: string | null | undefined;
  toggleRevision: number;
  updating: boolean;
  readmeLoading: boolean;
  onSelect: () => void;
  onApply: (game: SupportedGame, applied: boolean) => void;
  onClear: (game: SupportedGame) => void;
  onEdit: (game: SupportedGame) => void;
  onReadme: (game: SupportedGame) => void;
  onLog: (game: SupportedGame) => void;
};

export function GameCard({
  first, game, index, groupId, toggleRevision, updating, readmeLoading,
  onSelect, onApply, onClear, onEdit, onReadme, onLog,
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

  const toggle = (
    <ToggleField
      key={`${game.steam_app_id}-${toggleRevision}`}
      label={game.file_status === "partial" ? "Apply *" : "Apply"}
      checked={game.file_status === "complete"}
      disabled={updating || !game.game_path}
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
          {!game.manual && (
            <DialogButton
              style={{ flex: "0 0 30%", height: "36px", padding: "4px" }}
              onClick={() => onEdit(game)}
              disabled={updating || !game.game_path}
            >
              Edit
            </DialogButton>
          )}
          <DialogButton
            style={{ flex: game.manual ? "0 0 60%" : "0 0 30%", height: "36px", padding: "4px" }}
            onClick={() => onReadme(game)}
            disabled={readmeLoading}
          >
            ReadMe
          </DialogButton>
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
            <div>
              {game.file_status === "complete"
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
