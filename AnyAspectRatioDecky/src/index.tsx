import {
  ButtonItem,
  PanelSection,
  PanelSectionRow,
  staticClasses,
} from "@decky/ui";
import { callable, definePlugin } from "@decky/api";
import { useState } from "react";

type PluginStatus = {
  status: string;
  message: string;
};

const getPluginStatus = callable<[], PluginStatus>("get_plugin_status");

function Content() {
  const [message, setMessage] = useState("Backend status has not been checked.");
  const [checking, setChecking] = useState(false);

  const checkBackend = async () => {
    setChecking(true);
    try {
      const result = await getPluginStatus();
      setMessage(`${result.status}: ${result.message}`);
    } catch (error) {
      setMessage(`Backend check failed: ${String(error)}`);
    } finally {
      setChecking(false);
    }
  };

  return (
    <PanelSection>
      <PanelSectionRow>
        <ButtonItem layout="below" onClick={checkBackend} disabled={checking}>
          {checking ? "Checking backend..." : "Check backend"}
        </ButtonItem>
      </PanelSectionRow>
      <PanelSectionRow>
        <div role="status">{message}</div>
      </PanelSectionRow>
    </PanelSection>
  );
}

export default definePlugin(() => ({
  name: "AnyAspectRatio",
  titleView: (
    <div className={staticClasses.Title}>AnyAspectRatio</div>
  ),
  content: <Content />,
  icon: <span aria-hidden="true">AR</span>,
}));
