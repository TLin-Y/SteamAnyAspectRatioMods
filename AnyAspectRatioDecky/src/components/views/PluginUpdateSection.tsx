import { useState } from "react";
import { ButtonItem, ConfirmModal, Navigation, PanelSection, PanelSectionRow, showModal } from "@decky/ui";
import { checkForPluginUpdate, installPluginUpdateRequest, PLUGIN_NAME, PLUGIN_VERSION } from "../../services/callables";
import { errorText } from "../../services/formatters";
import { showErrorModal } from "../../services/modals";
import { DESKTOP_ACTIONS } from "../../services/platform";
import type { PluginUpdateInfo } from "../../services/types";

// The desktop app checks for its own release file (for this OS) and downloads it through the browser.
const DESKTOP = DESKTOP_ACTIONS !== null;
const PRODUCT = DESKTOP ? "app" : "plugin";

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
    setStatus(`Checking for a newer ${PRODUCT} release...`);
    try {
      const result = await checkForPluginUpdate();
      setUpdate(result);
      setStatus(
        !result.update_available ? `${PLUGIN_NAME} ${PLUGIN_VERSION} is up to date.`
          : !result.download_url ? `Version ${result.latest_version} exists but has no ${DESKTOP ? "download for this computer" : "plugin zip"} attached yet.`
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
      strTitle={`${DESKTOP ? "Download" : "Update to"} ${update?.latest_version}?`}
      strDescription={(DESKTOP
        ? `Opens the download of ${PLUGIN_NAME} ${update?.latest_version} for this computer in your browser. Close ${PLUGIN_NAME}, then use the downloaded file in place of the current app.`
        : `Downloads ${PLUGIN_NAME} ${update?.latest_version} from ${repositoryLabel} and replaces the installed plugin files.`
      ) + " Your settings and applied mods are kept."}
      strOKButtonText={DESKTOP ? "Download" : "Update"}
      onOK={DESKTOP ? () => update && Navigation.NavigateToExternalWeb(update.download_url) : install}
    />,
  );

  return (
    <PanelSection title={DESKTOP ? "App update" : "Plugin update"}>
      <PanelSectionRow>
        <ButtonItem
          layout="below"
          onClick={check}
          disabled={busy || offline}
          description={offline
            ? "Unavailable in offline mode."
            : `Installed: ${PLUGIN_VERSION}. Checks the releases of ${repositoryLabel}.`}
        >
          Check for {PRODUCT} update
        </ButtonItem>
      </PanelSectionRow>
      {update?.update_available && (
        <PanelSectionRow>
          <ButtonItem layout="below" onClick={confirmInstall} disabled={busy || !update.download_url}>
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
