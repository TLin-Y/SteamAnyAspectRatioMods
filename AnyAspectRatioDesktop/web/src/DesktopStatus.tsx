import { useEffect, useState, type ReactNode } from "react";
import { gamesByFolder, openFolder, systemInfo, type GameFolder, type SystemInfo } from "./decky-api";

const REFRESH_MS = 30_000;

function diskSize(bytes: number): string {
  const units = ["GB", "TB"];
  let value = bytes / 1024 ** 3;
  let unit = 0;
  while (value >= 1000 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }
  return `${value.toFixed(value >= 100 ? 0 : 1)} ${units[unit]}`;
}

/** A header item whose panel opens on hover or keyboard focus. */
function Dropdown({ label, title, onOpen, children }: {
  label: ReactNode;
  title?: string;
  onOpen?: () => void;
  children: ReactNode;
}) {
  return (
    <span className="aar-dropdown" tabIndex={0} title={title} onMouseEnter={onOpen} onFocus={onOpen}>
      <span className="aar-dropdown-label">{label}</span>
      <span className="aar-dropdown-menu">
        <span className="aar-dropdown-panel">{children}</span>
      </span>
    </span>
  );
}

function GamesList({ folders }: { folders: GameFolder[] | null }) {
  if (!folders) return <span className="aar-field-description">Loading...</span>;
  if (!folders.length) return <span className="aar-field-description">No installed games found.</span>;
  return (
    <>
      <span className="aar-field-description aar-legend">
        <span className="aar-dot aar-dot-on" /> has a mod <span className="aar-dot" /> no mod yet. Click to open the folder.
      </span>
      {folders.map(({ folder, games }) => (
        <span key={folder} className="aar-game-group">
          <button type="button" className="aar-menu-item aar-menu-heading" onClick={() => void openFolder(folder)}>
            {folder}
          </button>
          {games.map((game) => (
            <button key={game.path} type="button" className="aar-menu-item" title={game.path}
              onClick={() => void openFolder(game.path)}>
              <span className={game.supported ? "aar-dot aar-dot-on" : "aar-dot"} />
              {game.name}
            </button>
          ))}
        </span>
      ))}
    </>
  );
}

/** This computer at a glance, beside the app title. */
export function DesktopStatus() {
  const [info, setInfo] = useState<SystemInfo | null>(null);
  const [folders, setFolders] = useState<GameFolder[] | null>(null);

  useEffect(() => {
    const load = () => systemInfo().then(setInfo).catch(() => undefined);
    void load();
    const timer = window.setInterval(load, REFRESH_MS);
    window.addEventListener("focus", load);
    return () => {
      window.clearInterval(timer);
      window.removeEventListener("focus", load);
    };
  }, []);

  if (!info) return null;
  const games = info.games;
  return (
    <div className="aar-status">
      <span title={info.device}>{info.device}</span>
      <span>{info.os}</span>
      {info.disks.length > 0 && (
        <Dropdown label="Disks">
          {info.disks.map((disk) => (
            <span key={disk.path} className="aar-disk">
              <span className="aar-disk-path">{disk.path}</span>
              <span className="aar-disk-bar">
                <span style={{ width: `${100 - (disk.free_bytes / disk.total_bytes) * 100}%` }} />
              </span>
              <span>{diskSize(disk.free_bytes)} free of {diskSize(disk.total_bytes)}</span>
            </span>
          ))}
        </Dropdown>
      )}
      {games && (
        <Dropdown
          label={`${games.supported}/${games.installed} games`}
          title={`${games.supported} of the ${games.installed} installed games have a mod (last scan)`}
          onOpen={() => void gamesByFolder().then(setFolders).catch(() => setFolders([]))}
        >
          <GamesList folders={folders} />
        </Dropdown>
      )}
    </div>
  );
}
