import {
  ButtonItem,
  PanelSection,
  PanelSectionRow,
  staticClasses,
} from "@decky/ui";
import { callable, definePlugin } from "@decky/api";
import { useState } from "react";

type SupportedGameReport = {
  steam_scan: {
    libraries_scanned: number;
    apps_scanned: number;
  };
  catalogued_mods: number;
  supported_games: {
    mod_id: string;
    mod_name: string;
    steam_app_id: string;
    app_name: string;
    game_path: string | null;
    executable_path: string;
    executable_found: boolean;
    file_status: string;
    files_present: string[];
    files_missing: string[];
    ini_path: string | null;
    release_version: string;
  }[];
  warnings: string[];
};

const scanSupportedGames = callable<[], SupportedGameReport>("scan_supported_games");

function Content() {
  const [report, setReport] = useState<SupportedGameReport | null>(null);
  const [errorMessage, setErrorMessage] = useState("");
  const [scanning, setScanning] = useState(false);

  const scanForSupportedGames = async () => {
    setScanning(true);
    setReport(null);
    setErrorMessage("");
    try {
      setReport(await scanSupportedGames());
    } catch (error) {
      setErrorMessage(`Supported game scan failed: ${String(error)}`);
    } finally {
      setScanning(false);
    }
  };

  return (
    <PanelSection>
      <PanelSectionRow>
        <ButtonItem
          layout="below"
          onClick={scanForSupportedGames}
          disabled={scanning}
        >
          {scanning ? "Scanning Steam libraries..." : "Scan for supported games"}
        </ButtonItem>
      </PanelSectionRow>
      <PanelSectionRow>
        <div role="status">
          {errorMessage ||
            (report
              ? `Scanned ${report.steam_scan.apps_scanned} installed Steam apps across ${report.steam_scan.libraries_scanned} libraries. Found ${report.supported_games.length} supported installation(s); ${report.catalogued_mods} fix(es) are in the catalog.`
              : "Steam libraries have not been scanned.")}
        </div>
      </PanelSectionRow>
      {report?.supported_games.length === 0 && (
        <PanelSectionRow>
          <div role="status">No catalogued games were found in the scanned Steam libraries.</div>
        </PanelSectionRow>
      )}
      {report?.supported_games.map((game) => (
        <PanelSection key={`${game.steam_app_id}-${game.game_path}`}>
          <PanelSectionRow>
            <div>
              <strong>{game.app_name}</strong>
              <div>
                {game.mod_name} ({game.release_version})
              </div>
            </div>
          </PanelSectionRow>
          <PanelSectionRow>
            <div>
              {game.executable_found
                ? "Executable found"
                : "Expected executable not found"}
              {game.game_path ? `: ${game.executable_path}` : ""}
            </div>
          </PanelSectionRow>
          <PanelSectionRow>
            <div>
              {game.file_status === "complete"
                ? "All catalogued mod files detected"
                : game.file_status === "partial"
                  ? `Some mod files detected (${game.files_present.length}/${
                      game.files_present.length + game.files_missing.length
                    })`
                  : "No catalogued mod files detected"}
            </div>
          </PanelSectionRow>
          {game.ini_path && (
            <PanelSectionRow>
              <div>INI: {game.ini_path}</div>
            </PanelSectionRow>
          )}
        </PanelSection>
      ))}
      {report?.warnings.map((warning, index) => (
        <PanelSectionRow key={`${index}-${warning}`}>
          <div role="status">{warning}</div>
        </PanelSectionRow>
      ))}
    </PanelSection>
  );
}

export default definePlugin(() => ({
  name: "AnyAspectRatio",
  titleView: (
    <div className={staticClasses.Title}>AnyAspectRatio</div>
  ),
  content: <Content />,
  icon: <span aria-hidden="true">AR</span>,
}));
