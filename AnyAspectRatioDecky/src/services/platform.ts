// The desktop app swaps this module for its own copy at build time.
// Typing straight into the Quick Access panel drops on-screen-keyboard keys, so the Deck edits text in a dialog.
import type { SupportedGame } from "./types";

export const INLINE_TEXT_INPUT = false;

/** Extra game-card buttons that only make sense on a desktop. */
export type DesktopActions = {
  openFolder: (game: SupportedGame) => void;
  showWineOverrides: (game: SupportedGame) => void;
  /** Resolutions this computer runs games at, offered as one-click INI defaults. */
  resolutionChoices: () => Promise<ResolutionChoice[]>;
};

export type ResolutionChoice = { label: string; width: number; height: number };

export const DESKTOP_ACTIONS: DesktopActions | null = null;
