import { DialogButton, TextField } from "@decky/ui";
import { INLINE_TEXT_INPUT } from "../../services/platform";
import { editInDialog } from "../modals/TextInputModal";

const CLEAR_BUTTON_STYLE = { flex: "0 0 auto", minWidth: "64px", width: "auto", padding: "6px 12px" };

export function GameFilter({ value, onChange }: { value: string; onChange: (value: string) => void }) {
  const clear = value ? (
    <DialogButton style={CLEAR_BUTTON_STYLE} onClick={() => onChange("")}>
      Clear
    </DialogButton>
  ) : null;

  if (INLINE_TEXT_INPUT) {
    return (
      <div className="aar-filter" style={{ display: "flex", alignItems: "center", gap: "8px", width: "100%" }}>
        <div style={{ flex: 1, minWidth: 0 }}>
          <TextField
            value={value}
            aria-label="Filter games by name"
            onChange={(event) => onChange(event.currentTarget.value)}
          />
        </div>
        {clear}
      </div>
    );
  }

  const edit = async () => {
    const next = await editInDialog("Filter games by name", value);
    if (next !== null) onChange(next.trim());
  };
  return (
    <div style={{ display: "flex", alignItems: "center", gap: "8px", width: "100%" }}>
      <DialogButton
        style={{ flex: 1, minWidth: 0, padding: "6px 12px", textAlign: "left", overflowWrap: "anywhere" }}
        onClick={edit}
      >
        {value ? `Filter: ${value}` : "Filter games by name"}
      </DialogButton>
      {clear}
    </div>
  );
}
