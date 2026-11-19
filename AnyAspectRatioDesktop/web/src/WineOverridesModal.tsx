import { useEffect, useState } from "react";
import { copyToClipboard } from "../../../AnyAspectRatioDecky/src/services/clipboard";
import { errorText } from "../../../AnyAspectRatioDecky/src/services/formatters";
import { showErrorModal } from "../../../AnyAspectRatioDecky/src/services/modals";
import { requiredLaunchOption } from "../../../AnyAspectRatioDecky/src/services/modToggle";
import type { SupportedGame } from "../../../AnyAspectRatioDecky/src/services/types";
import { launchAction } from "./decky-api";
import { ConfirmModal, DialogButton, TextField } from "./decky-ui";

type WineInfo = {
  bottle: { name: string; tool: string } | null;
  message?: string;
  current?: string;
  required: string;
};

/** The one thing to get right under Wine: which DLLs the game loads from its own folder. */
export function WineOverridesModal({ game, closeModal }: { game: SupportedGame; closeModal?: () => void }) {
  const [info, setInfo] = useState<WineInfo | null>(null);
  const [value, setValue] = useState("");
  const [status, setStatus] = useState("Reading the Wine settings...");

  useEffect(() => {
    launchAction<WineInfo>("info", [game.steam_app_id, game.mod_id])
      .then((loaded) => {
        setInfo(loaded);
        setValue(loaded.current ?? "");
        setStatus("");
      })
      .catch((error) => setStatus(`Could not read the Wine settings: ${errorText(error)}`));
  }, []);

  const save = () => {
    launchAction("set", [game.steam_app_id, game.mod_id, value])
      .catch((error) => showErrorModal("Could not save the Wine overrides", errorText(error)));
  };

  if (info && !info.bottle) {
    const launchOption = requiredLaunchOption(game);
    return (
      <ConfirmModal bAlertDialog strTitle={`Wine DLL overrides: ${game.app_name}`} strOKButtonText="Close"
        strDescription={info.message} closeModal={closeModal}>
        <div className="aar-info-status">{launchOption}</div>
        <div className="aar-button-row">
          <DialogButton onClick={() => void copyToClipboard(launchOption).then(() => setStatus("Copied."))}>
            Copy
          </DialogButton>
        </div>
        {status && <div role="status" className="aar-info-status">{status}</div>}
      </ConfirmModal>
    );
  }

  return (
    <ConfirmModal
      strTitle={`Wine DLL overrides: ${game.app_name}`}
      strDescription={info?.bottle
        ? `Set in the ${info.bottle.tool} bottle "${info.bottle.name}" for this game only. The mod needs ${info.required}.`
        : undefined}
      strOKButtonText="Save"
      strCancelButtonText="Cancel"
      bOKDisabled={!info}
      onOK={save}
      closeModal={closeModal}
    >
      {info && (
        <TextField
          aria-label={info.required}
          value={value}
          onChange={(event) => setValue(event.currentTarget.value)}
        />
      )}
      {status && <div role="status" className="aar-info-status">{status}</div>}
    </ConfirmModal>
  );
}
