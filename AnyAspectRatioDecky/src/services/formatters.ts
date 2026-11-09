import type { updateDependencies } from "./callables";

export function formatCatalogUpdate(result: Awaited<ReturnType<typeof updateDependencies>>): string {
  const summary = `${result.catalog_changed ? "Catalog updated" : "Catalog already up to date"}: ` +
    `${result.mod_count} mod(s), ${result.local_archive_count} local archive(s). ` +
    "Cached mod downloads were refreshed.";
  return [summary, ...result.warnings].join("\n");
}

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 ** 2) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes >= 1024 ** 3) return `${(bytes / 1024 ** 3).toFixed(2)} GB`;
  return `${(bytes / 1024 ** 2).toFixed(1)} MB`;
}

export function formatRelativeTime(dateString: string): string {
  const date = new Date(dateString);
  const now = new Date();
  const diffMs = now.getTime() - date.getTime();
  const diffSecs = Math.floor(diffMs / 1000);

  if (diffSecs < 60) return "just now";
  const diffMins = Math.floor(diffSecs / 60);
  if (diffMins < 60) return `${diffMins}m ago`;
  const diffHours = Math.floor(diffMins / 60);
  if (diffHours < 24) return `${diffHours}h ago`;
  const diffDays = Math.floor(diffHours / 24);
  const remainingHours = diffHours % 24;
  if (remainingHours > 0) return `${diffDays}d${remainingHours}h ago`;
  return `${diffDays}d ago`;
}

export const MOD_LOG_BUTTON_STYLE = {
  flex: "0 0 22%",
  width: "22%",
  minWidth: 0,
  maxWidth: "25%",
  padding: "0 4px",
  height: "36px",
};
