import { ConfirmModal, TextField, showModal } from "@decky/ui";
import { useState } from "react";

function TextInputModal({
  title,
  initial,
  numeric,
  onDone,
  closeModal,
}: {
  title: string;
  initial: string;
  numeric: boolean;
  onDone: (value: string | null) => void;
  closeModal?: () => void;
}) {
  const [value, setValue] = useState(initial);
  return (
    <ConfirmModal
      strTitle={title}
      strOKButtonText="OK"
      onOK={() => onDone(value)}
      onCancel={() => onDone(null)}
      closeModal={closeModal}
    >
      <TextField
        focusOnMount
        mustBeNumeric={numeric}
        value={value}
        onChange={(event) => setValue(event.currentTarget.value)}
      />
    </ConfirmModal>
  );
}

// Typing happens in a Steam dialog: in the Quick Access panel, touch typing on the
// on-screen keyboard drops keys because the panel window loses focus on every tap.
export function editInDialog(title: string, initial: string, numeric = false): Promise<string | null> {
  return new Promise((resolve) => {
    showModal(
      <TextInputModal title={title} initial={initial} numeric={numeric} onDone={resolve} />,
      undefined,
      { fnOnClose: () => resolve(null) },
    );
  });
}
