import { ConfirmModal, DialogButton } from "@decky/ui";
import { useState } from "react";
import { copyToClipboard } from "../../services/modals";
import type { LaunchOptionsDisplay } from "../../services/types";

export function LaunchOptionsInfoModal({
  title,
  message,
  values,
  closeModal,
}: {
  title: string;
  message: string;
  values: LaunchOptionsDisplay[];
  closeModal: () => void;
}) {
  const [copyStatus, setCopyStatus] = useState("");

  return (
    <ConfirmModal
      bAlertDialog
      bAllowFullSize
      strTitle={title}
      strDescription={message}
      strOKButtonText="Close"
      onOK={closeModal}
      onCancel={closeModal}
    >
      {values.map(({ label, value }) => (
        <div key={label} style={{ marginTop: "12px" }}>
          <strong>{label}</strong>
          <div
            style={{
              maxHeight: "18vh",
              overflowY: "auto",
              overflowWrap: "anywhere",
              whiteSpace: "pre-wrap",
              margin: "6px 0",
            }}
          >
            {value || "(empty)"}
          </div>
          <DialogButton
            onClick={async () => {
              try {
                await copyToClipboard(value);
                setCopyStatus(`${label} copied.`);
              } catch (error) {
                setCopyStatus(`Copy failed: ${String(error)}`);
              }
            }}
          >
            Copy
          </DialogButton>
        </div>
      ))}
      {copyStatus && (
        <div role="status" style={{ marginTop: "8px" }}>
          {copyStatus}
        </div>
      )}
    </ConfirmModal>
  );
}
