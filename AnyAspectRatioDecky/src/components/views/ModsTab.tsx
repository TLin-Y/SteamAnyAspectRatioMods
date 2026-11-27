import { Fragment, useEffect, useState, useSyncExternalStore } from "react";
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
import { CleanupModal } from "../modals/CleanupModal";
import { APPLIED_BLUE, errorText, formatBytes, formatCatalogUpdate } from "../../services/formatters";
import type { CreditsInfo } from "../../services/types";

const EXTERNAL_GRAY = "#9e9e9e";

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

type Author = CreditsInfo["authors"][number];

const Divider = () => (
  <PanelSectionRow>
    <div style={{ width: "100%", borderTop: "1px solid rgba(255,255,255,0.25)", margin: "8px 0" }} />
  </PanelSectionRow>
);

const APPLIED_STYLE = { color: APPLIED_BLUE, fontWeight: "bold" } as const;

// "https://codeberg.org/Lyall" -> "Codeberg"
const siteName = (url: string) => {
  const site = new URL(url).hostname.replace(/^www\./, "").split(".")[0];
  return site.charAt(0).toUpperCase() + site.slice(1);
};

const showAuthorMods = (author: Author, applied: number) =>
  showModal(
    <ConfirmModal
      strTitle={`${author.name} - ${applied}/${author.mod_count} mods`}
      strOKButtonText="Close"
      bAlertDialog
    >
      <div className="aar-scroll-pane" style={{ maxHeight: "60vh", overflowY: "auto" }}>
        {author.mods.map((mod, index) => (
          <div
            key={mod.id}
            style={{
              display: "flex", gap: "8px", padding: "4px 8px", borderRadius: "3px",
              background: index % 2 ? "transparent" : "rgba(255,255,255,0.05)",
              ...(mod.applied ? APPLIED_STYLE : { color: "#6b6b6b" }),
            }}
          >
            <span style={{ minWidth: "2.5em", textAlign: "right", flexShrink: 0 }}>{index + 1}.</span>
            <span>{mod.name}</span>
          </div>
        ))}
      </div>
    </ConfirmModal>,
  );

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
      <CleanupModal
        onConfirm={(choice) => runAction(
          "Delete failed",
          () => clearLocalData(choice.caches, choice.logs, choice.downloads),
          "Selected local files deleted.",
          onDataChanged,
        )}
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
      {credits && <Divider />}
      {credits && (
        <PanelSectionRow>
          <div style={{ fontSize: "13px" }}>
            Thank you to every author below. Your free, public fixes let us enjoy our games on the screens we love. ❤️
          </div>
        </PanelSectionRow>
      )}
      {credits?.authors.map((author, index, authors) => {
        const links = [
          ...(author.url ? [{ label: siteName(author.url), url: author.url }] : []),
          ...author.support,
        ];
        const applied = author.mods.filter((mod) => mod.applied).length;
        return (
          <Fragment key={`${author.name}|${author.url}`}>
          {author.external && !authors[index - 1]?.external && (
            <PanelSectionRow>
              <div style={{ color: EXTERNAL_GRAY }}>
                <div style={{ fontWeight: "bold" }}>External Source (untested)</div>
                <div style={{ fontSize: "12px" }}>
                  These mods may not be redistributed, but they are free for anyone to download and use. Please visit
                  the original page for more details.
                </div>
              </div>
            </PanelSectionRow>
          )}
          <PanelSectionRow>
            <div style={{ width: "100%", marginBottom: "12px" }}>
              <DialogButton
                style={{
                  width: "100%", minWidth: 0, padding: "8px", fontSize: "15px", marginBottom: "4px",
                  ...(author.external ? { color: EXTERNAL_GRAY } : {}),
                }}
                onClick={() => showAuthorMods(author, applied)}
              >
                <span style={applied ? APPLIED_STYLE : undefined}>{author.name}</span>
                {" - "}
                <span style={applied ? APPLIED_STYLE : undefined}>{applied}</span>
                {`/${author.mod_count} mods`}
              </DialogButton>
              <div style={{ display: "flex", gap: "4px", flexWrap: "wrap" }}>
                {links.map((link) => (
                  <DialogButton
                    key={link.url}
                    style={{
                      flex: 1, minWidth: 0, minHeight: 0, height: "26px", padding: "0 4px", fontSize: "12px",
                      background: "transparent", border: "1px solid rgba(255,255,255,0.25)",
                      overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap",
                    }}
                    disabled={credits.offline_mode}
                    onClick={() => Navigation.NavigateToExternalWeb(link.url)}
                  >
                    {link.label}
                  </DialogButton>
                ))}
              </div>
            </div>
          </PanelSectionRow>
          </Fragment>
        );
      })}
      <Divider />
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
