import { useState } from "react";
import { ButtonItem, ConfirmModal, PanelSection, PanelSectionRow, showModal } from "@decky/ui";
import { checkForPluginUpdate, installPluginUpdateRequest, PLUGIN_VERSION } from "../../services/callables";
import { errorText } from "../../services/formatters";
import { showErrorModal } from "../../services/modals";
import type { PluginUpdateInfo } from "../../services/types";

type Props = {
  busy: boolean;
  setBusy: (busy: boolean) => void;
  offline: boolean;
  /** "owner/repo" whose GitHub releases are checked. */
  repositoryLabel: string;
};

function showManualInstall(file: string, size: number, savedZip: string) {
  const sizeMb = (size / (1024 * 1024)).toFixed(2);
  showModal(
    <ConfirmModal
      bAlertDialog
      strTitle="Update downloaded - install it manually"
      strDescription={
        `File: ${file}\nSize: ${size} bytes (${sizeMb} MB)\nSaved to: ${savedZip}\n\n` +
        "Decky plugins cannot replace themselves, so finish the update yourself:\n" +
        "1. Open Decky Settings > Developer.\n" +
        "2. Choose Install Plugin from ZIP.\n" +
        `3. Select ${file} from the Downloads folder and confirm.`
      }
      strOKButtonText="OK"
    />,
  );
}

/** Self-update from GitHub releases. Only shown in the GitHub build; store installs update through the store. */
export function PluginUpdateSection({ busy, setBusy, offline, repositoryLabel }: Props) {
  const [update, setUpdate] = useState<PluginUpdateInfo | null>(null);
  const [status, setStatus] = useState("");

  const check = async () => {
    setBusy(true);
    setUpdate(null);
    setStatus("Checking for a newer plugin release...");
    try {
      const result = await checkForPluginUpdate();
      if (result.error) throw new Error(result.error);
      setUpdate(result);
      setStatus(
        !result.update_available ? `AnyAspectRatio ${PLUGIN_VERSION} is up to date.`
          : !result.download_url ? `Version ${result.latest_version} exists but has no plugin zip attached yet.`
          : `Version ${result.latest_version} is available.`,
      );
    } catch (error) {
      setStatus("");
      showErrorModal("Plugin update check failed", errorText(error));
    } finally {
      setBusy(false);
    }
  };

  const install = async () => {
    setBusy(true);
    setStatus("Downloading and installing the update...");
    try {
      const result = await installPluginUpdateRequest();
      if (result.error) throw new Error(result.error);
      if (result.installed) {
        setUpdate(null);
        setStatus(result.restarted
          ? `Updated to ${result.version}. Decky is reloading the plugin.`
          : `Updated to ${result.version}. Restart Decky Loader or reboot to finish.`);
      } else {
        setStatus(`Downloaded ${result.file_name}. Install it manually (see the popup).`);
        showManualInstall(result.file_name ?? "", result.size_bytes ?? 0, result.saved_zip ?? "");
      }
    } catch (error) {
      setStatus("");
      showErrorModal("Plugin update failed", errorText(error));
    } finally {
      setBusy(false);
    }
  };

  const confirmInstall = () => showModal(
    <ConfirmModal
      strTitle={`Update to ${update?.latest_version}?`}
      strDescription={`Downloads AnyAspectRatio ${update?.latest_version} from ${repositoryLabel} and replaces the installed plugin files. Your settings and applied mods are kept.`}
      strOKButtonText="Update"
      onOK={install}
    />,
  );

  return (
    <PanelSection title="Plugin update">
      <PanelSectionRow>
        <ButtonItem
          layout="below"
          onClick={check}
          disabled={busy || offline}
          description={offline
            ? "Unavailable in offline mode."
            : `Installed: ${PLUGIN_VERSION}. Checks the releases of ${repositoryLabel}.`}
        >
          Check for plugin update
        </ButtonItem>
      </PanelSectionRow>
      {update?.update_available && (
        <PanelSectionRow>
          <ButtonItem layout="below" onClick={confirmInstall} disabled={busy}>
            Update to {update.latest_version}
          </ButtonItem>
        </PanelSectionRow>
      )}
      {status && (
        <PanelSectionRow>
          <div role="status" style={{ overflowWrap: "anywhere" }}>{status}</div>
        </PanelSectionRow>
      )}
    </PanelSection>
  );
}
