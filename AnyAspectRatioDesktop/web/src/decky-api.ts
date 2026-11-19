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

export function callable<Args extends unknown[], Result>(name: string): (...args: Args) => Promise<Result> {
  return async (...args: Args) => {
    const api = await ready;
    try {
      return (await api.call(name, args)) as Result;
    } catch (error) {
      throw errorFrom(error);
    }
  };
}

export async function launchAction<Result>(action: string, args: unknown[]): Promise<Result> {
  const api = await ready;
  try {
    return (await api.launch(action, args)) as Result;
  } catch (error) {
    throw errorFrom(error);
  }
}

export type Disk = { path: string; free_bytes: number; total_bytes: number };
export type SystemInfo = {
  device: string;
  os: string;
  disks: Disk[];
  games: { supported: number; installed: number } | null;
};
export type VirtualDesktop = { label: string; width: number; height: number };
export type GameFolder = { folder: string; games: { name: string; path: string; supported: boolean }[] };

export async function gamesByFolder(): Promise<GameFolder[]> {
  return (await (await ready).games_by_folder()) as GameFolder[];
}

export async function systemInfo(): Promise<SystemInfo> {
  return (await (await ready).system_info()) as SystemInfo;
}

export async function virtualDesktops(): Promise<VirtualDesktop[]> {
  return (await (await ready).virtual_desktops()) as VirtualDesktop[];
}

export async function openFolder(folder: string): Promise<void> {
  const api = await ready;
  try {
    await api.open_folder(folder);
  } catch (error) {
    throw errorFrom(error);
  }
}

export async function openUrl(url: string): Promise<void> {
  await (await ready).open_url(url);
}

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
  const picked = (await (await ready).pick_path(select === FileSelectionType.FOLDER, extensions ?? [])) as string | null;
  if (!picked) throw new Error("Picker was dismissed.");
  return { path: picked, realpath: picked };
}

export function definePlugin<T>(factory: () => T): () => T {
  return factory;
}
