import { createRoot } from "react-dom/client";
import plugin from "../../../AnyAspectRatioDecky/src/index";
import { ModalHost } from "./decky-ui";
import { DesktopStatus } from "./DesktopStatus";

const { titleView, content } = plugin();

createRoot(document.getElementById("root")!).render(
  <>
    <header className="aar-header">
      {titleView}
      <DesktopStatus />
    </header>
    <main className="aar-content">{content}</main>
    <ModalHost />
  </>,
);
