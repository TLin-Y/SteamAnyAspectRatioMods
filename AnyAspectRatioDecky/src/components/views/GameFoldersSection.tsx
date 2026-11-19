import { ButtonItem, DialogButton, PanelSection, PanelSectionRow } from "@decky/ui";
import { openFilePicker } from "@decky/api";
import type { FileSelectionType } from "@decky/api";
import { addCustomGameDir, removeCustomGameDir } from "../../services/callables";

export function GameFoldersSection({
  folders,
  disabled,
  onChanged,
  onError,
}: {
  folders: string[];
  disabled: boolean;
  onChanged: () => Promise<void>;
  onError: (title: string, error: unknown) => void;
}) {
  const add = async () => {
    let picked;
    try {
      picked = await openFilePicker(1 as FileSelectionType, "/home", false, true);
    } catch {
      return; // Picker was dismissed.
    }
    try {
      await addCustomGameDir(picked.realpath || picked.path);
      await onChanged();
    } catch (error) {
      onError("Could not add the game folder", error);
    }
  };

  const remove = async (folder: string) => {
    try {
      await removeCustomGameDir(folder);
      await onChanged();
    } catch (error) {
      onError("Could not remove the game folder", error);
    }
  };

  return (
    <PanelSection title="Game folders">
      <PanelSectionRow>
        <div style={{ fontSize: "0.8em", opacity: 0.8 }}>
          For games outside Steam. Add a game's folder, or a folder that holds several games.
        </div>
      </PanelSectionRow>
      {folders.map((folder) => (
        <PanelSectionRow key={folder}>
          <div style={{ display: "flex", alignItems: "center", gap: "8px", width: "100%" }}>
            <div style={{ flex: 1, minWidth: 0, overflowWrap: "anywhere" }}>{folder}</div>
            <DialogButton
              style={{ flex: "0 0 auto", width: "auto", minWidth: "80px", padding: "6px 12px" }}
              disabled={disabled}
              onClick={() => void remove(folder)}
            >
              Remove
            </DialogButton>
          </div>
        </PanelSectionRow>
      ))}
      <PanelSectionRow>
        <ButtonItem layout="below" disabled={disabled} onClick={() => void add()}>
          Add game folder...
        </ButtonItem>
      </PanelSectionRow>
    </PanelSection>
  );
}
