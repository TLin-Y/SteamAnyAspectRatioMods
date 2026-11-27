import { ConfirmModal, DialogCheckbox } from "@decky/ui";
import { useState } from "react";

export type CleanupChoice = { caches: boolean; logs: boolean; downloads: boolean };

export function CleanupModal({
  onConfirm,
  closeModal,
}: {
  onConfirm: (choice: CleanupChoice) => void;
  closeModal?: () => void;
}) {
  const [choice, setChoice] = useState<CleanupChoice>({ caches: true, logs: true, downloads: false });
  const option = (key: keyof CleanupChoice, label: string, description: string) => (
    <DialogCheckbox
      label={label}
      description={description}
      checked={choice[key]}
      controlled
      onChange={(checked) => setChoice((current) => ({ ...current, [key]: checked }))}
    />
  );

  return (
    <ConfirmModal
      strTitle="Delete local files?"
      strDescription="Settings and mods already applied to games are kept."
      strOKButtonText="Delete"
      bOKDisabled={!choice.caches && !choice.logs && !choice.downloads}
      onOK={() => onConfirm(choice)}
      closeModal={closeModal}
    >
      {option("caches", "Plugin cache", "The imported repository zip, the downloaded catalog and mod archives.")}
      {option("logs", "Plugin logs", "The plugin's own log files.")}
      {option(
        "downloads",
        "Saved author downloads",
        "The plugin's copies of zips you downloaded from mod authors. Removing those mods later may need the zip again.",
      )}
    </ConfirmModal>
  );
}
