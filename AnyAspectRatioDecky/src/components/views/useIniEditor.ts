import { useRef, useState } from "react";
import { getModIni, saveModIni } from "../../services/callables";
import type { SupportedGame } from "../../services/types";

type Options = {
  /** The game whose INI is open, and its key; re-read on every render. */
  game: SupportedGame | undefined;
  gameKey: (game: SupportedGame) => string;
  /** Shared detail-view request counter: a newer request cancels pending results. */
  requestId: { current: number };
  notifyError: (title: string, message: string) => void;
  refresh: () => Promise<string | null>;
};

/** Line-by-line INI editor for one game. A field saves when it loses focus; saves never overlap. */
export function useIniEditor({ game, gameKey, requestId, notifyError, refresh }: Options) {
  const [openKey, setOpenKey] = useState<string | null>(null);
  const [lines, setLines] = useState<string[]>([]);
  const [savedContent, setSavedContent] = useState("");
  const [exists, setExists] = useState(false);
  const [loaded, setLoaded] = useState(false);
  const [saving, setSaving] = useState(false);
  const [status, setStatus] = useState("");
  const savingRef = useRef(false);
  const pendingSave = useRef(false);
  const saveRef = useRef<() => Promise<void>>(async () => {});

  const close = () => {
    savingRef.current = false;
    pendingSave.current = false;
    setOpenKey(null);
    setSaving(false);
  };

  const open = async (target: SupportedGame, request: number) => {
    close();
    setLines([]);
    setSavedContent("");
    setExists(false);
    setLoaded(false);
    setStatus("");
    if (!target.ini_file || !target.game_path) return;
    setOpenKey(gameKey(target));
    try {
      const ini = await getModIni(target.mod_id, target.game_path);
      if (request !== requestId.current) return;
      setLines(ini.content.split(/\r\n|\n|\r/));
      setSavedContent(ini.content);
      setExists(ini.exists);
      setLoaded(true);
    } catch (error) {
      if (request === requestId.current) notifyError("Could not load configuration", String(error));
    }
  };

  const save = async () => {
    if (!game?.game_path || !game.ini_file || openKey !== gameKey(game)) return;
    if (savingRef.current) {
      pendingSave.current = true;
      return;
    }
    const content = lines.join("\n");
    if (content === savedContent) return;
    const request = requestId.current;
    savingRef.current = true;
    setSaving(true);
    setStatus("");
    try {
      const result = await saveModIni(game.mod_id, game.game_path, content);
      if (request !== requestId.current) return;
      setSavedContent(content);
      setExists(true);
      setStatus(result.permissions_preserved
        ? "Configuration saved."
        : "Configuration saved, but this filesystem could not preserve the file permissions.");
    } catch (error) {
      if (request === requestId.current) notifyError("Could not save configuration", String(error));
      return;
    } finally {
      if (request === requestId.current) {
        savingRef.current = false;
        setSaving(false);
        if (pendingSave.current) {
          pendingSave.current = false;
          window.setTimeout(() => void saveRef.current(), 0);
        }
      }
    }
    const refreshError = await refresh();
    if (refreshError && request === requestId.current) {
      notifyError("Configuration saved, but status refresh failed", refreshError);
    }
  };
  saveRef.current = save;

  const onBlur = () => {
    if (!loaded) return;
    if (savingRef.current) pendingSave.current = true;
    else if (lines.join("\n") !== savedContent) void save();
  };

  const onChange = (index: number, value: string) => {
    const updated = [...lines];
    updated[index] = value;
    setLines(updated);
    setStatus("");
  };

  return { state: { lines, loaded, exists, saving, status }, open, close, onBlur, onChange };
}
