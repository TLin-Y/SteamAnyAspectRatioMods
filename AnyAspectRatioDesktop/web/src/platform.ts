// Replaces AnyAspectRatioDecky/src/services/platform.ts in the desktop build.
import { createElement } from "react";
import type { DesktopActions, ResolutionChoice } from "../../../AnyAspectRatioDecky/src/services/platform";
import { showErrorModal } from "../../../AnyAspectRatioDecky/src/services/modals";
import { errorText } from "../../../AnyAspectRatioDecky/src/services/formatters";
import { openFolder, virtualDesktops } from "./decky-api";
import { showModal } from "./decky-ui";
import { WineOverridesModal } from "./WineOverridesModal";

export type { DesktopActions, ResolutionChoice };

// A real keyboard types straight into fields.
export const INLINE_TEXT_INPUT = true;

export const DESKTOP_ACTIONS: DesktopActions = {
  openFolder: (game) => {
    if (!game.game_path) return;
    openFolder(game.game_path).catch((error) => showErrorModal("Could not open the folder", errorText(error)));
  },
  // Games list no overrides on Windows, so the button only shows where Wine runs the game.
  showWineOverrides: (game) => {
    showModal(createElement(WineOverridesModal, { game }));
  },
  resolutionChoices: async () => {
    const desktops = await virtualDesktops().catch(() => []);
    return [displayResolution(), ...desktops];
  },
};

/** The display as games see it: Wine on macOS works in points (unless CrossOver's high resolution mode is on). */
function displayResolution(): ResolutionChoice {
  const scale = navigator.userAgent.includes("Mac") ? 1 : window.devicePixelRatio || 1;
  // The browser reports a whole number of scaled pixels (1707 at 150% for 2560), so scaling back can be
  // half a pixel off; real display modes are even.
  const physical = (size: number) => Math.round((size * scale) / 2) * 2;
  return { label: "display", width: physical(window.screen.width), height: physical(window.screen.height) };
}
