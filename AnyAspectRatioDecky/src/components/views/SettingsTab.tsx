import { useEffect, useRef, useState } from "react";
import {
  ButtonItem,
  ConfirmModal,
  PanelSection,
  PanelSectionRow,
  TextField,
  ToggleField,
  showModal,
} from "@decky/ui";
import { openFilePicker } from "@decky/api";
import type { FileSelectionType } from "@decky/api";
import {
  getPluginSettings,
  savePluginSettings,
  setOfflineMode,
  updateDependencies,
  checkForPluginUpdate,
  installPluginUpdateRequest,
  getLog,
  PLUGIN_VERSION,
} from "../../services/callables";
import { showErrorModal } from "../../services/modals";
import type {
  PluginSettingsValues,
  PluginSettingsInfo,
  PluginUpdateInfo,
} from "../../services/types";

export function SettingsTab({ onDataChanged }: { onDataChanged: () => void }) {
  const [info, setInfo] = useState<PluginSettingsInfo | null>(null);
  const [values, setValues] = useState<PluginSettingsValues>({
    catalog_repository_url: "",
    catalog_ref: "",
    catalog_path: "",
    local_archive_dir: "",
  });
  const [busy, setBusy] = useState(false);
  const savedValues = useRef("");
  const [status, setStatus] = useState("");
  const [pluginUpdate, setPluginUpdate] = useState<PluginUpdateInfo | null>(null);
  const [pluginStatus, setPluginStatus] = useState("");

  const repositoryLabel = (
    values.catalog_repository_url || "the configured repository"
  ).replace("https://github.com/", "");

  const checkPluginUpdate = async () => {
    setBusy(true);
    setPluginUpdate(null);
    setPluginStatus("Checking for a newer plugin release...");
    try {
      const result = await checkForPluginUpdate();
      if (result.error) throw new Error(result.error);
      setPluginUpdate(result);
      if (!result.update_available) {
        setPluginStatus(`AnyAspectRatio ${PLUGIN_VERSION} is up to date.`);
      } else if (!result.download_url) {
        setPluginStatus(
          `Version ${result.latest_version} exists but has no plugin zip attached yet.`,
        );
      } else {
        setPluginStatus(`Version ${result.latest_version} is available.`);
      }
    } catch (error) {
      setPluginStatus("");
      showErrorModal(
        "Plugin update check failed",
        error instanceof Error ? error.message : String(error),
      );
    } finally {
      setBusy(false);
    }
  };

  const installPluginUpdate = async () => {
    setBusy(true);
    setPluginStatus("Downloading and installing the update...");
    try {
      const result = await installPluginUpdateRequest();
      if (result.error) throw new Error(result.error);
      if (result.installed) {
        setPluginUpdate(null);
        setPluginStatus(
          result.restarted
            ? `Updated to ${result.version}. Decky is reloading the plugin.`
            : `Updated to ${result.version}. Restart Decky Loader or reboot to finish.`,
        );
      } else {
        const sizeMb = ((result.size_bytes ?? 0) / (1024 * 1024)).toFixed(2);
        setPluginStatus(`Downloaded ${result.file_name}. Install it manually (see the popup).`);
        showModal(
          <ConfirmModal
            bAlertDialog
            strTitle="Update downloaded - install it manually"
            strDescription={
              `File: ${result.file_name}\n` +
              `Size: ${result.size_bytes} bytes (${sizeMb} MB)\n` +
              `Saved to: ${result.saved_zip}\n\n` +
              "Decky plugins cannot replace themselves, so finish the update yourself:\n" +
              "1. Open Decky Settings > Developer.\n" +
              "2. Choose Install Plugin from ZIP.\n" +
              `3. Select ${result.file_name} from the Downloads folder and confirm.`
            }
            strOKButtonText="OK"
          />,
        );
      }
    } catch (error) {
      setPluginStatus("");
      showErrorModal(
        "Plugin update failed",
        error instanceof Error ? error.message : String(error),
      );
    } finally {
      setBusy(false);
    }
  };

  const confirmPluginUpdate = () =>
    showModal(
      <ConfirmModal
        strTitle={`Update to ${pluginUpdate?.latest_version}?`}
        strDescription={`Downloads AnyAspectRatio ${pluginUpdate?.latest_version} from ${repositoryLabel} and replaces the installed plugin files. Your settings and applied mods are kept.`}
        strOKButtonText="Update"
        onOK={installPluginUpdate}
      />,
    );

  const showError = (title: string, error: unknown) => {
    const message = error instanceof Error ? error.message : String(error);
    setStatus(`${title}: ${message}`);
    showErrorModal(title, message);
  };

  const load = async () => {
    const loaded = await getPluginSettings();
    setInfo(loaded);
    const loadedValues = {
      catalog_repository_url:
        loaded.settings.catalog_repository_url || loaded.defaults.catalog_repository_url,
      catalog_ref: loaded.settings.catalog_ref || loaded.defaults.catalog_ref,
      catalog_path: loaded.settings.catalog_path || loaded.defaults.catalog_path,
      local_archive_dir: loaded.settings.local_archive_dir,
    };
    savedValues.current = JSON.stringify(loadedValues);
    setValues(loadedValues);
  };

  useEffect(() => {
    load().catch((error) => showError("Could not load settings", error));
  }, []);

  const setValue = (key: keyof PluginSettingsValues, value: string) =>
    setValues((current) => ({ ...current, [key]: value }));

  const runUpdate = async () => {
    const result = await updateDependencies();
    if (result.error) {
      throw new Error(result.error);
    }
    setStatus(
      `${result.catalog_changed ? "Catalog updated" : "Catalog already up to date"}: ` +
        `${result.mod_count} mod(s), ${result.local_archive_count} local archive(s). ` +
        "Cached mod downloads were refreshed.",
    );
    await load();
    onDataChanged();
  };

  const save = async (next: PluginSettingsValues = values) => {
    setBusy(true);
    setStatus("");
    try {
      const result = await savePluginSettings(
        next.catalog_repository_url,
        next.catalog_ref,
        next.catalog_path,
        next.local_archive_dir,
      );
      if (result.source_changed && !info?.settings.offline_mode) {
        setStatus("Settings saved. Fetching the catalog from the new source...");
        await runUpdate();
      } else {
        setStatus("Settings saved.");
        await load();
        onDataChanged();
      }
    } catch (error) {
      showError("Could not save settings", error);
    } finally {
      setBusy(false);
    }
  };

  const update = async () => {
    setBusy(true);
    setStatus("Checking the repository for updates...");
    try {
      await runUpdate();
    } catch (error) {
      showError("Update failed", error);
    } finally {
      setBusy(false);
    }
  };

  const chooseFolder = async () => {
    try {
      const picked = await openFilePicker(
        1 as FileSelectionType,
        values.local_archive_dir || "/home",
        false,
        true,
      );
      const next = { ...values, local_archive_dir: picked.realpath || picked.path };
      setValues(next);
      await save(next);
    } catch {
      // Picker was dismissed.
    }
  };

  const resetToDefaults = async () => {
    if (!info) return;
    const next = { ...info.defaults, local_archive_dir: "" };
    setValues(next);
    await save(next);
  };

  const viewLog = async () => {
    try {
      const log = await getLog(300);
      if (log.error) throw new Error(log.error);
      showModal(
        <ConfirmModal
          bAlertDialog
          strTitle="Plugin log (latest lines)"
          strOKButtonText="Close"
          strDescription={
            <div>
              <div style={{ fontSize: "0.8em", opacity: 0.7, overflowWrap: "anywhere" }}>
                {log.path}
              </div>
              <pre
                style={{
                  maxHeight: "50vh",
                  overflow: "auto",
                  fontSize: "0.7em",
                  whiteSpace: "pre-wrap",
                  overflowWrap: "anywhere",
                }}
              >
                {log.content || "The log is empty."}
              </pre>
            </div>
          }
        />,
      );
    } catch (error) {
      showError("Could not read the log", error);
    }
  };

  const changeOfflineMode = async (enabled: boolean) => {
    setBusy(true);
    try {
      const result = await setOfflineMode(enabled);
      setStatus(
        result.warning ? `Offline mode is off. ${result.warning}` : enabled
          ? "Offline mode is on. The plugin makes no internet connections."
          : "Offline mode is off.",
      );
      onDataChanged();
      await load();
    } catch (error) {
      showError("Could not change offline mode", error);
    } finally {
      setBusy(false);
    }
  };

  const field = (label: string, key: keyof PluginSettingsValues) => (
    <PanelSectionRow>
      <div style={{ width: "100%" }}>
        <div style={{ fontSize: "0.85em", opacity: 0.8 }}>{label}</div>
        <TextField
          value={values[key]}
          disabled={busy || !info}
          onBlur={() => {
            if (info && savedValues.current !== JSON.stringify(values)) void save();
          }}
          onChange={(e) => setValue(key, e.currentTarget.value)}
        />
      </div>
    </PanelSectionRow>
  );

  return (
    <>
      <PanelSection title="Privacy">
        <PanelSectionRow>
          <ToggleField
            label="Offline mode"
            description="Locks the plugin so it never connects to the internet. Mods then install only from an imported offline pack or your local mod folder."
            checked={info?.settings.offline_mode ?? false}
            disabled={busy || !info}
            onChange={changeOfflineMode}
          />
        </PanelSectionRow>
      </PanelSection>
      <PanelSection title="Plugin update">
        <PanelSectionRow>
          <ButtonItem
            layout="below"
            onClick={checkPluginUpdate}
            disabled={busy || !info || info.settings.offline_mode}
            description={
              info?.settings.offline_mode
                ? "Unavailable in offline mode."
                : `Installed: ${PLUGIN_VERSION}. Checks the releases of ${repositoryLabel}.`
            }
          >
            Check for plugin update
          </ButtonItem>
        </PanelSectionRow>
        {pluginUpdate?.update_available && (
          <PanelSectionRow>
            <ButtonItem layout="below" onClick={confirmPluginUpdate} disabled={busy}>
              Update to {pluginUpdate.latest_version}
            </ButtonItem>
          </PanelSectionRow>
        )}
        {pluginStatus && (
          <PanelSectionRow>
            <div role="status" style={{ overflowWrap: "anywhere" }}>{pluginStatus}</div>
          </PanelSectionRow>
        )}
      </PanelSection>
      <PanelSection title="Troubleshooting">
        <PanelSectionRow>
          <ButtonItem layout="below" disabled={busy} onClick={viewLog}>
            View log
          </ButtonItem>
        </PanelSectionRow>
      </PanelSection>
      <PanelSection title="Mod source (GitHub)">
        {field("Repository", "catalog_repository_url")}
        {field("Branch, tag, or commit", "catalog_ref")}
        {field("Catalog file path", "catalog_path")}
        <PanelSectionRow>
          <div role="status" style={{ overflowWrap: "anywhere" }}>
            {info
              ? info.catalog.origin === "cache"
                ? `Using downloaded catalog (${info.catalog.mod_count} mods, fetched ${
                    info.catalog.fetched_at
                      ? new Date(info.catalog.fetched_at).toLocaleString()
                      : "earlier"
                  }).`
                : `Using the catalog bundled with the plugin (${info.catalog.mod_count} mods). Press Update to fetch the latest.`
              : "Loading settings..."}
          </div>
        </PanelSectionRow>
        {info?.catalog.warning && (
          <PanelSectionRow>
            <div role="status">{info.catalog.warning}</div>
          </PanelSectionRow>
        )}
        <PanelSectionRow>
          <ButtonItem
            layout="below"
            onClick={update}
            disabled={busy || !info || info.settings.offline_mode}
          >
            Update catalog &amp; mods
          </ButtonItem>
        </PanelSectionRow>
      </PanelSection>
      <PanelSection title="Local mod archives">
        {field("Folder containing *.zip mods", "local_archive_dir")}
        <PanelSectionRow>
          <ButtonItem layout="below" onClick={chooseFolder} disabled={busy || !info}>
            Choose folder...
          </ButtonItem>
        </PanelSectionRow>
        <PanelSectionRow>
          <div role="status" style={{ overflowWrap: "anywhere" }}>
            Name each zip after its game (for example "Game Name-v1.0.zip") or include its
            Steam app ID in brackets. Contents are extracted next to the game's executable.
            {info && info.settings.local_archive_dir
              ? ` ${info.local_archives.count} valid archive(s) found.`
              : ""}
          </div>
        </PanelSectionRow>
        {info?.local_archives.warnings.map((warning, index) => (
          <PanelSectionRow key={`${index}-${warning}`}>
            <div role="status" style={{ overflowWrap: "anywhere" }}>{warning}</div>
          </PanelSectionRow>
        ))}
      </PanelSection>
      <PanelSection title="Settings">
        <PanelSectionRow>
          <ButtonItem layout="below" onClick={resetToDefaults} disabled={busy || !info}>
            Reset to defaults
          </ButtonItem>
        </PanelSectionRow>
        {status && (
          <PanelSectionRow>
            <div role="status" style={{ overflowWrap: "anywhere" }}>{status}</div>
          </PanelSectionRow>
        )}
        {info && (
          <PanelSectionRow>
            <div style={{ overflowWrap: "anywhere", fontSize: "0.8em", opacity: 0.7 }}>
              Data and cache: {info.paths.data_dir}
              <br />
              Log file: {info.paths.log_file}
            </div>
          </PanelSectionRow>
        )}
      </PanelSection>
    </>
  );
}
