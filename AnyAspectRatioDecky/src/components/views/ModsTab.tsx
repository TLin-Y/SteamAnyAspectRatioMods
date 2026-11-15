import { useEffect, useState, useSyncExternalStore } from "react";
import {
  ButtonItem,
  ConfirmModal,
  DialogButton,
  Navigation,
  PanelSection,
  PanelSectionRow,
  showModal,
} from "@decky/ui";
import { openFilePicker } from "@decky/api";
import type { FileSelectionType } from "@decky/api";
import { getCredits, importOfflinePack, clearLocalData, updateDependencies, DEFAULT_PICKER_DIRECTORY } from "../../services/callables";
import { showErrorModal } from "../../services/modals";
import { errorText, formatBytes, formatCatalogUpdate } from "../../services/formatters";
import type { CreditsInfo } from "../../services/types";

type ActionState = { busy: boolean; status: string; completions: number };

// Lives outside the component: the tab unmounts when switching tabs or closing the menu,
// but a running import must keep showing its progress when the tab is shown again.
let actionState: ActionState = { busy: false, status: "", completions: 0 };
const actionListeners = new Set<() => void>();

const updateActionState = (patch: Partial<ActionState>) => {
  actionState = { ...actionState, ...patch };
  actionListeners.forEach((listener) => listener());
};

const subscribeActionState = (listener: () => void) => {
  actionListeners.add(listener);
  return () => {
    actionListeners.delete(listener);
  };
};

const getActionState = () => actionState;

async function runAction<T extends { warning?: string | null; [key: string]: unknown }>(
  title: string,
  action: () => Promise<T>,
  doneMessage: string | ((result: T) => string),
  onDone: () => void,
  pendingMessage = "Working...",
) {
  if (actionState.busy) return;
  updateActionState({ busy: true, status: pendingMessage });
  try {
    const result = await action();
    const message = typeof doneMessage === "function" ? doneMessage(result) : doneMessage;
    updateActionState({ status: result.warning ? `${message} ${result.warning}` : message });
    onDone();
  } catch (reason) {
    updateActionState({ status: "" });
    showErrorModal(title, errorText(reason));
  } finally {
    updateActionState({ busy: false, completions: actionState.completions + 1 });
  }
}

export function ModsTab({ onDataChanged }: { onDataChanged: () => void }) {
  const [credits, setCredits] = useState<CreditsInfo | null>(null);
  const [error, setError] = useState("");
  const { busy, status, completions } = useSyncExternalStore(subscribeActionState, getActionState);

  useEffect(() => {
    let cancelled = false;
    getCredits().then(
      (value) => {
        if (!cancelled) {
          setCredits(value);
          setError("");
        }
      },
      (reason) => {
        if (!cancelled) setError(errorText(reason));
      },
    );
    return () => {
      cancelled = true;
    };
  }, [completions]);

  const importRepositoryZip = async () => {
    let picked;
    try {
      picked = await openFilePicker(
        0 as FileSelectionType,
        DEFAULT_PICKER_DIRECTORY,
        true,
        false,
        undefined,
        ["zip"],
      );
    } catch {
      return;
    }
    await runAction(
      "Import failed",
      () => importOfflinePack(picked.realpath || picked.path),
      "Offline pack imported.",
      onDataChanged,
    );
  };

  const confirmDeleteLocalData = () =>
    showModal(
      <ConfirmModal
        strTitle="Delete local files and caches?"
        strDescription="Removes the imported repository zip, the downloaded catalog, caches, and plugin logs. Settings and mods already applied to games are kept."
        strOKButtonText="Delete"
        onOK={() => runAction("Delete failed", clearLocalData, "Local files and caches deleted.", onDataChanged)}
      />,
    );

  const pack = credits?.pack;
  return (
    <PanelSection title="Mods">
      <PanelSectionRow>
        <ButtonItem
          layout="below"
          disabled={busy || !credits || credits.offline_mode}
          onClick={() => runAction(
            "Update failed",
            updateDependencies,
            formatCatalogUpdate,
            onDataChanged,
            "Checking the repository for updates...",
          )}
        >
          Update catalog &amp; mods
        </ButtonItem>
      </PanelSectionRow>
      {error && <PanelSectionRow>{error}</PanelSectionRow>}
      {credits && (
        <PanelSectionRow>
          <div style={{ fontSize: "12px", opacity: 0.7 }}>
            {credits.mod_count} mod(s) by {credits.authors.length} author(s) -{" "}
            {pack?.file_count
              ? `local files: ${pack.file_count}, ${formatBytes(pack.size_bytes)}`
              : "no offline pack imported"}
          </div>
        </PanelSectionRow>
      )}
      {status && (
        <PanelSectionRow>
          <div role="status" style={{ fontSize: "12px", whiteSpace: "pre-wrap", overflowWrap: "anywhere" }}>{status}</div>
        </PanelSectionRow>
      )}
      {credits?.authors.map((author) => {
        const links = [
          ...(author.url ? [{ label: author.url.replace("https://", ""), url: author.url }] : []),
          ...author.support,
        ];
        return (
          <PanelSectionRow key={`${author.name}|${author.url}`}>
            <div style={{ width: "100%" }}>
              <div>{`${author.name} - ${author.mod_count} mod(s)`}</div>
              <div style={{ display: "flex", gap: "4px", flexWrap: "wrap" }}>
                {links.map((link) => (
                  <DialogButton
                    key={link.url}
                    style={{ flex: 1, minWidth: 0, minHeight: 0, height: "28px", padding: "0 4px", fontSize: "12px" }}
                    disabled={credits.offline_mode}
                    onClick={() => Navigation.NavigateToExternalWeb(link.url)}
                  >
                    {link.label}
                  </DialogButton>
                ))}
              </div>
            </div>
          </PanelSectionRow>
        );
      })}
      <PanelSectionRow>
        <div style={{ width: "100%", borderTop: "1px solid rgba(255,255,255,0.25)", margin: "8px 0" }} />
      </PanelSectionRow>
      <PanelSectionRow>
        <ButtonItem layout="below" disabled={busy} onClick={importRepositoryZip}>
          Import offline mods pack
        </ButtonItem>
      </PanelSectionRow>
      <PanelSectionRow>
        <ButtonItem layout="below" disabled={busy} onClick={confirmDeleteLocalData}>
          Delete all local files and caches
        </ButtonItem>
      </PanelSectionRow>
    </PanelSection>
  );
}
