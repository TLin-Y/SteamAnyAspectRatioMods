import { createRoot } from "react-dom/client";
import plugin from "../../../AnyAspectRatioDecky/src/index";
import { openUrl } from "./decky-api";
import { ModalHost } from "./decky-ui";
import { DesktopStatus } from "./DesktopStatus";

const { titleView, content } = plugin();
// The user guide lives in this README only; the logo opens it in the browser.
const GUIDE_URL = "https://github.com/TLin-Y/SteamAnyAspectRatioMods/blob/main/AnyAspectRatioDesktop/README.md";

// Links (mostly in mod ReadMes) must not load into the app's only window: web pages open in the browser,
// anything else (a ReadMe's relative link) does nothing.
document.addEventListener("click", (event) => {
  const link = (event.target as Element | null)?.closest?.("a[href]") as HTMLAnchorElement | null;
  if (!link) return;
  event.preventDefault();
  if (/^https?:/.test(link.href)) void openUrl(link.href);
}, true);

createRoot(document.getElementById("root")!).render(
  <>
    <header className="aar-header">
      <button className="aar-brand" title="Open the user guide" onClick={() => void openUrl(GUIDE_URL)}>
        {titleView}
      </button>
      <DesktopStatus />
    </header>
    <main className="aar-content">{content}</main>
    <ModalHost />
  </>,
);
