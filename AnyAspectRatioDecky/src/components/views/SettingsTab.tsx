import { useEffect, useState } from "react";
import {
  ButtonItem,
  ConfirmModal,
  DialogButton,
  PanelSection,
  PanelSectionRow,
  ToggleField,
  showModal,
} from "@decky/ui";
import { openFilePicker } from "@decky/api";
import type { FileSelectionType } from "@decky/api";
import {
  getPluginSettings,
  updateCurrentResolution,
  savePluginSettings,
  setOfflineMode,
  updateDependencies,
  getLog,
} from "../../services/callables";
import { showErrorModal } from "../../services/modals";
import { errorText, formatCatalogUpdate } from "../../services/formatters";
import { PluginUpdateSection } from "./PluginUpdateSection";
import { EditableValue } from "./EditableValue";
import { GameFoldersSection } from "./GameFoldersSection";
import { DESKTOP_ACTIONS, type ResolutionChoice } from "../../services/platform";
import { useRemembered } from "../../services/remembered";
import type { PluginSettingsInfo, PluginSettingsValues } from "../../services/types";

type SettingsFormValues = PluginSettingsValues & {
  ini_resolution_width: string;
  ini_resolution_height: string;
};

const PENDING_STYLE = { color: "#ff9830", fontWeight: "bold" } as const;

function modCount(count: number): string {
  return `${count} mod${count === 1 ? "" : "s"}`;
}

export function SettingsTab({ onDataChanged }: { onDataChanged: () => void }) {
  const [info, setInfo] = useState<PluginSettingsInfo | null>(null);
  const [values, setValues] = useState<SettingsFormValues>({
    catalog_repository_url: "",
    catalog_ref: "",
    catalog_path: "",
    local_archive_dir: "",
    ini_resolution_width: "",
    ini_resolution_height: "",
  });
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState("");
  const [resolutionStatus, setResolutionStatus] = useState("");
  const [showMore, setShowMore] = useRemembered("settings.showMore", false);
  const [resolutionChoices, setResolutionChoices] = useState<ResolutionChoice[]>([]);

  const repositoryLabel = (
    values.catalog_repository_url || "the configured repository"
  ).replace("https://github.com/", "");

  const showError = (title: string, error: unknown) => {
    const message = errorText(error);
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
      ini_resolution_width: String(loaded.settings.ini_resolution_width),
      ini_resolution_height: String(loaded.settings.ini_resolution_height),
    };
    setValues(loadedValues);
  };

  useEffect(() => {
    load().catch((error) => showError("Could not load settings", error));
    DESKTOP_ACTIONS?.resolutionChoices().then(setResolutionChoices).catch(() => undefined);
  }, []);

  const runUpdate = async () => {
    const result = await updateDependencies();
    setStatus(formatCatalogUpdate(result));
    await load();
    onDataChanged();
  };

  const save = async (next: SettingsFormValues) => {
    const resolutionChanged = next.ini_resolution_width !== values.ini_resolution_width ||
      next.ini_resolution_height !== values.ini_resolution_height;
    setValues(next);
    setBusy(true);
    setStatus("");
    if (resolutionChanged) setResolutionStatus("");
    try {
      const result = await savePluginSettings(
        next.catalog_repository_url,
        next.catalog_ref,
        next.catalog_path,
        next.local_archive_dir,
        next.ini_resolution_width,
        next.ini_resolution_height,
      );
      if (result.source_changed && !info?.settings.offline_mode) {
        setStatus("Settings saved. Fetching the catalog from the new source...");
        await runUpdate();
      } else {
        setStatus("Settings saved.");
        const archiveDirChanged = next.local_archive_dir !== info?.settings.local_archive_dir;
        await load();
        if (archiveDirChanged) onDataChanged();
      }
      if (resolutionChanged) setResolutionStatus("Resolution saved.");
    } catch (error) {
      await load().catch(() => undefined);
      if (resolutionChanged) setResolutionStatus("Could not save the resolution.");
      showError("Could not save settings", error);
    } finally {
      setBusy(false);
    }
  };

  const updateCurrent = async () => {
    setBusy(true);
    setResolutionStatus("Scanning installed mods...");
    try {
      const result = await updateCurrentResolution();
      setInfo((current) => current && {
        ...current,
        settings: {
          ...current.settings,
          ini_resolution_update_pending: result.resolution_update_pending,
        },
      });
      const summary = `Updated ${modCount(result.updated.length)}.` +
        (result.unchanged.length ? ` ${modCount(result.unchanged.length)} already up to date.` : "");
      const problems = [
        ...result.failures.map((message) => `Failed: ${message}`),
        ...result.skipped.map((message) => `Skipped: ${message}`),
      ];
      setResolutionStatus(problems.length ? `${summary} ${problems.length} need attention.` : summary);
      showModal(
        <ConfirmModal
          bAlertDialog
          strTitle="Update current mods"
          strDescription={[summary, ...problems].join("\n")}
          strOKButtonText="OK"
        />,
      );
    } catch (error) {
      setResolutionStatus("Could not update current mods.");
      showError("Could not update current mods", error);
    } finally {
      setBusy(false);
    }
  };

  const confirmUpdateCurrent = () => showModal(
    <ConfirmModal
      strTitle="Update current mods?"
      strDescription={`Scan installed mods and update catalogued INI resolution values to ${info?.settings.ini_resolution_width} x ${info?.settings.ini_resolution_height}. Other settings and zero values are kept. This overwrites any existing resolution values in those fields.`}
      strOKButtonText="Update current"
      onOK={updateCurrent}
    />,
  );

  const chooseFolder = async () => {
    try {
      const picked = await openFilePicker(
        1 as FileSelectionType,
        values.local_archive_dir || "/home",
        false,
        true,
      );
      await save({ ...values, local_archive_dir: picked.realpath || picked.path });
    } catch {
      // Picker was dismissed.
    }
  };

  const resetToDefaults = async () => {
    if (!info) return;
    const next: SettingsFormValues = {
      ...info.defaults,
      local_archive_dir: "",
      ini_resolution_width: String(info.defaults.ini_resolution_width),
      ini_resolution_height: String(info.defaults.ini_resolution_height),
    };
    await save(next);
  };

  const viewLog = async () => {
    try {
      const log = await getLog(300);
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

  const field = (label: string, key: keyof SettingsFormValues, numeric = false) => (
    <PanelSectionRow>
      <EditableValue
        label={label}
        value={values[key]}
        numeric={numeric}
        disabled={busy || !info}
        onCommit={(value) => void save({ ...values, [key]: value })}
      />
    </PanelSectionRow>
  );

  return (
    <>
      <PanelSection title="Privacy">
        <PanelSectionRow>
          <ToggleField
            label="Offline mode"
            description="Blocks all internet connections from this plugin. This plugin has zero tracking or upload code."
            checked={info?.settings.offline_mode ?? false}
            disabled={busy || !info}
            onChange={changeOfflineMode}
          />
        </PanelSectionRow>
      </PanelSection>
      <PanelSection title="Default mod INI resolution">
        {field("Width (pixels)", "ini_resolution_width", true)}
        {field("Height (pixels)", "ini_resolution_height", true)}
        {resolutionChoices.length > 0 && (
          <PanelSectionRow>
            <div style={{ display: "flex", flexWrap: "wrap", gap: "8px", width: "100%" }}>
              {resolutionChoices.map((choice) => (
                <DialogButton
                  key={`${choice.label}-${choice.width}x${choice.height}`}
                  style={{ flex: "1 1 auto", width: "auto", minWidth: 0, padding: "6px 12px" }}
                  disabled={busy || !info}
                  onClick={() => void save({
                    ...values,
                    ini_resolution_width: String(choice.width),
                    ini_resolution_height: String(choice.height),
                  })}
                >
                  {`Use ${choice.label}: ${choice.width} x ${choice.height}`}
                </DialogButton>
              ))}
            </div>
          </PanelSectionRow>
        )}
        <PanelSectionRow>
          <ButtonItem layout="below" disabled={busy || !info} onClick={confirmUpdateCurrent}>
            <span style={info?.settings.ini_resolution_update_pending ? PENDING_STYLE : undefined}>
              Update current{info?.settings.ini_resolution_update_pending ? " *" : ""}
            </span>
          </ButtonItem>
        </PanelSectionRow>
        {info?.settings.ini_resolution_update_pending && (
          <PanelSectionRow>
            <div role="status" style={PENDING_STYLE}>
              Saved resolution has not been used to update current mods. Select Update current to apply it.
            </div>
          </PanelSectionRow>
        )}
        {resolutionStatus && (
          <PanelSectionRow>
            <div role="status">{resolutionStatus}</div>
          </PanelSectionRow>
        )}
        <PanelSectionRow>
          <div style={{ fontSize: "0.8em", opacity: 0.8 }}>
            Only some mods support this.
          </div>
        </PanelSectionRow>
      </PanelSection>
      <GameFoldersSection
        folders={info?.settings.custom_game_dirs ?? []}
        disabled={busy || !info}
        onChanged={async () => {
          await load();
          onDataChanged();
        }}
        onError={showError}
      />
      {info?.self_update_enabled && (
        <PluginUpdateSection
          busy={busy}
          setBusy={setBusy}
          offline={info.settings.offline_mode}
          repositoryLabel={repositoryLabel}
        />
      )}
      <PanelSection title="Troubleshooting">
        <PanelSectionRow>
          <ButtonItem layout="below" disabled={busy} onClick={viewLog}>
            View log
          </ButtonItem>
        </PanelSectionRow>
      </PanelSection>
      <PanelSection>
        <PanelSectionRow>
          <ButtonItem layout="below" onClick={() => setShowMore(!showMore)}>
            {showMore ? "Hide more settings" : "More settings"}
          </ButtonItem>
        </PanelSectionRow>
      </PanelSection>
      {showMore && (
        <>
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
                    : `Using the catalog bundled with the plugin (${info.catalog.mod_count} mods). Update it from the Mods tab.`
                  : "Loading settings..."}
              </div>
            </PanelSectionRow>
            {info?.catalog.warning && (
              <PanelSectionRow>
                <div role="status">{info.catalog.warning}</div>
              </PanelSectionRow>
            )}
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
      )}
    </>
  );
}
