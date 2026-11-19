type DesktopApi = Record<string, (...args: unknown[]) => Promise<unknown>>;

declare global {
  interface Window {
    pywebview?: { api: DesktopApi };
  }
}

const ready: Promise<DesktopApi> = new Promise((resolve) => {
  const done = () => resolve(window.pywebview!.api);
  if (window.pywebview?.api) done();
  else window.addEventListener("pywebviewready", done, { once: true });
});

function errorFrom(raw: unknown): Error {
  if (raw instanceof Error) return raw;
  if (raw && typeof raw === "object" && "message" in raw) return new Error(String((raw as { message: unknown }).message));
  return new Error(String(raw));
}

/** Calls a method of app.py's Api; Python exceptions arrive as plain objects and become Errors. */
async function invoke<Result>(method: string, ...args: unknown[]): Promise<Result> {
  const api = await ready;
  try {
    return (await api[method](...args)) as Result;
  } catch (error) {
    throw errorFrom(error);
  }
}

export function callable<Args extends unknown[], Result>(name: string): (...args: Args) => Promise<Result> {
  return (...args: Args) => invoke<Result>("call", name, args);
}

export const launchAction = <Result>(action: string, args: unknown[]) => invoke<Result>("launch", action, args);

export type Disk = { path: string; free_bytes: number; total_bytes: number };
export type SystemInfo = {
  device: string;
  os: string;
  disks: Disk[];
  games: { supported: number; installed: number } | null;
};
export type VirtualDesktop = { label: string; width: number; height: number };
export type GameFolder = { folder: string; games: { name: string; path: string; supported: boolean }[] };

export const gamesByFolder = () => invoke<GameFolder[]>("games_by_folder");
export const systemInfo = () => invoke<SystemInfo>("system_info");
export const virtualDesktops = () => invoke<VirtualDesktop[]>("virtual_desktops");
export const openFolder = (folder: string) => invoke<void>("open_folder", folder);
export const openUrl = (url: string) => invoke<void>("open_url", url);

export enum FileSelectionType {
  FILE = 0,
  FOLDER = 1,
}

export type FilePickerRes = { path: string; realpath: string };

export async function openFilePicker(
  select: FileSelectionType,
  _startPath?: string,
  _includeFiles?: boolean,
  _includeFolders?: boolean,
  _filter?: unknown,
  extensions?: string[],
): Promise<FilePickerRes> {
  const picked = await invoke<string | null>("pick_path", select === FileSelectionType.FOLDER, extensions ?? []);
  if (!picked) throw new Error("Picker was dismissed.");
  return { path: picked, realpath: picked };
}

export function definePlugin<T>(factory: () => T): () => T {
  return factory;
}
