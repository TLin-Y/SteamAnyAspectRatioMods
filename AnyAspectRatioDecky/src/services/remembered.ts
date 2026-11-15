import { useState } from "react";

// Steam unmounts the Quick Access panel content while a dialog is open, so navigation
// state kept only in React would reset to the first tab when the dialog closes.
const values = new Map<string, unknown>();

export function remember<T>(key: string, value: T): void {
  values.set(key, value);
}

/** useState whose value outlives the component, for state that must survive a remount. */
export function useRemembered<T>(key: string, initial: T): [T, (value: T) => void] {
  const [value, setValue] = useState<T>(values.has(key) ? (values.get(key) as T) : initial);
  const set = (next: T) => {
    values.set(key, next);
    setValue(next);
  };
  return [value, set];
}
