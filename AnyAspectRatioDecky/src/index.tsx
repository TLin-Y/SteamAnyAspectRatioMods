import {
  staticClasses,
  PanelSection,
  PanelSectionRow,
} from "@decky/ui";
import { definePlugin } from "@decky/api";
import { useState, useEffect } from "react";

import { PLUGIN_VERSION, getOfflineMode } from "./services/callables";
import GamesTab from "./components/views/GamesTab";
import { SettingsTab } from "./components/views/SettingsTab";
import { ModsTab } from "./components/views/ModsTab";

type TabId = "games" | "mods" | "settings";

function Content() {
  const [tab, setTab] = useState<TabId>("games");
  const [dataRevision, setDataRevision] = useState(0);
  const [offlineMode, setOfflineMode] = useState(false);

  useEffect(() => {
    getOfflineMode()
      .then(setOfflineMode)
      .catch((error) => console.error("Could not read offline mode:", error));
  }, [dataRevision]);

  const tabs: Array<{ id: TabId; label: string }> = [
    { id: "games", label: "Games" },
    { id: "mods", label: "Mods" },
    { id: "settings", label: "Settings" },
  ];

  return (
    <>
      <PanelSection>
        <PanelSectionRow>
          <div
            style={{
              display: "flex",
              gap: "8px",
              width: "100%",
            }}
          >
            {tabs.map((t) => (
              <button
                key={t.id}
                onClick={() => setTab(t.id)}
                style={{
                  flex: 1,
                  padding: "8px 12px",
                  backgroundColor: tab === t.id ? "rgba(255,255,255,0.2)" : "transparent",
                  border: "1px solid rgba(255,255,255,0.3)",
                  borderRadius: "4px",
                  color: "white",
                  cursor: "pointer",
                  fontSize: "0.9em",
                }}
              >
                {t.label}
              </button>
            ))}
          </div>
        </PanelSectionRow>
        {offlineMode && (
          <PanelSectionRow>
            <div role="status" style={{ fontSize: "0.85em", opacity: 0.85 }}>
              🛡️ Offline mode: no internet connections
            </div>
          </PanelSectionRow>
        )}
      </PanelSection>
      <div style={{ display: tab === "games" ? "block" : "none" }}>
        <GamesTab dataRevision={dataRevision} />
      </div>
      {tab === "mods" && (
        <ModsTab onDataChanged={() => setDataRevision((revision) => revision + 1)} />
      )}
      {tab === "settings" && (
        <SettingsTab onDataChanged={() => setDataRevision((revision) => revision + 1)} />
      )}
    </>
  );
}

export default definePlugin(() => ({
  name: "AnyAspectRatio",
  titleView: (
    <div className={staticClasses.Title}>
      AnyAspectRatio <span style={{ fontSize: "0.7em", opacity: 0.7 }}>{PLUGIN_VERSION}</span>
    </div>
  ),
  content: <Content />,
  icon: <span aria-hidden="true">AR</span>,
}));
