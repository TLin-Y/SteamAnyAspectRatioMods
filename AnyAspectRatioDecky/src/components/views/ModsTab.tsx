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
import { getCredits, importOfflinePack, downloadOfflinePack, clearLocalData, DEFAULT_PICKER_DIRECTORY } from "../../services/callables";
import { showErrorModal } from "../../services/modals";
import { formatBytes } from "../../services/formatters";
import type { CreditsInfo } from "../../services/types";

type ActionState = { busy: boolean; downloading: boolean; status: string; completions: number };

// Lives outside the component: the tab unmounts when switching tabs or closing the menu,
// but a running download must keep showing its progress when the tab is shown again.
let actionState: ActionState = { busy: false, downloading: false, status: "", completions: 0 };
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

async function runAction(
  title: string,
  action: () => Promise<{ error?: string; version?: string; warning?: string | null }>,
  doneMessage: string | ((result: { version?: string }) => string),
  onDone: () => void,
  pendingMessage = "Working...",
  downloading = false,
) {
  if (actionState.busy) return;
  updateActionState({ busy: true, downloading, status: pendingMessage });
  try {
    const result = await action();
    if (result.error) throw new Error(result.error);
    const message = typeof doneMessage === "function" ? doneMessage(result) : doneMessage;
    updateActionState({ status: result.warning ? `${message} ${result.warning}` : message });
    onDone();
  } catch (reason) {
    updateActionState({ status: "" });
    showErrorModal(title, reason instanceof Error ? reason.message : String(reason));
  } finally {
    updateActionState({ busy: false, downloading: false, completions: actionState.completions + 1 });
  }
}

export function ModsTab({ onDataChanged }: { onDataChanged: () => void }) {
  const [credits, setCredits] = useState<CreditsInfo | null>(null);
  const [error, setError] = useState("");
  const { busy, downloading, status, completions } = useSyncExternalStore(subscribeActionState, getActionState);

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
        if (!cancelled) setError(String(reason));
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
      "Repository zip imported.",
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

  const confirmDownloadOfflinePack = () =>
    showModal(
      <ConfirmModal
        strTitle="Download offline pack (5 mins)?"
        strDescription="Downloads the offline pack from the latest release. This may use around 500 MB or more and replaces the current offline pack and catalog with the release versions."
        strOKButtonText="Download"
        onOK={() => runAction(
          "Download failed",
          downloadOfflinePack,
          (result) => `Offline pack ${result.version ?? ""} downloaded and imported.`,
          onDataChanged,
          "Downloading and importing the offline pack. This may take several minutes...",
          true,
        )}
      />,
    );

  const pack = credits?.pack;
  return (
    <PanelSection title="Mods">
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
          <div style={{ fontSize: "12px" }}>{status}</div>
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
        <ButtonItem
          layout="below"
          disabled={busy || !credits || credits.offline_mode}
          onClick={confirmDownloadOfflinePack}
        >
          {downloading ? "Downloading..." : "Download offline pack (5 mins)"}
        </ButtonItem>
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
