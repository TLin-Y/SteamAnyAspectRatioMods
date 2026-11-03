import {
  ButtonItem,
  PanelSection,
  PanelSectionRow,
  staticClasses,
} from "@decky/ui";
import { callable, definePlugin } from "@decky/api";
import { useState } from "react";

type SteamScanReport = {
  libraries_scanned: number;
  apps_scanned: number;
  apps: {
    app_id: string;
    name: string;
    install_dir: string;
    library_path: string;
    game_path: string | null;
  }[];
  warnings: string[];
};

const scanSteamLibraries = callable<[], SteamScanReport>("scan_steam_libraries");

function Content() {
  const [report, setReport] = useState<SteamScanReport | null>(null);
  const [errorMessage, setErrorMessage] = useState("");
  const [scanning, setScanning] = useState(false);

  const scanLibraries = async () => {
    setScanning(true);
    setReport(null);
    setErrorMessage("");
    try {
      setReport(await scanSteamLibraries());
    } catch (error) {
      setErrorMessage(`Steam library scan failed: ${String(error)}`);
    } finally {
      setScanning(false);
    }
  };

  return (
    <PanelSection>
      <PanelSectionRow>
        <ButtonItem layout="below" onClick={scanLibraries} disabled={scanning}>
          {scanning ? "Scanning Steam libraries..." : "Scan Steam libraries"}
        </ButtonItem>
      </PanelSectionRow>
      <PanelSectionRow>
        <div role="status">
          {errorMessage ||
            (report
              ? `Scanned ${report.apps_scanned} installed Steam apps across ${report.libraries_scanned} libraries.`
              : "Steam libraries have not been scanned.")}
        </div>
      </PanelSectionRow>
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
