import { DialogButton } from "@decky/ui";
import { editInDialog } from "../modals/TextInputModal";

/** A value shown as a button; pressing it edits the value in a dialog, and OK commits it. */
export function EditableValue({
  label,
  value,
  onCommit,
  numeric = false,
  disabled = false,
  placeholder = "Not set",
}: {
  label?: string;
  value: string;
  onCommit: (value: string) => void;
  numeric?: boolean;
  disabled?: boolean;
  placeholder?: string;
}) {
  const edit = async () => {
    const next = await editInDialog(label ?? "Edit", value, numeric);
    if (next !== null && next !== value) onCommit(next);
  };
  return (
    <div style={{ width: "100%" }}>
      {label && <div style={{ fontSize: "0.85em", opacity: 0.8 }}>{label}</div>}
      <DialogButton
        disabled={disabled}
        onClick={edit}
        style={{
          width: "100%",
          minWidth: 0,
          padding: "6px 12px",
          textAlign: "left",
          overflowWrap: "anywhere",
          whiteSpace: "pre-wrap",
          opacity: value ? 1 : 0.6,
        }}
      >
        {value || placeholder}
      </DialogButton>
    </div>
  );
}
