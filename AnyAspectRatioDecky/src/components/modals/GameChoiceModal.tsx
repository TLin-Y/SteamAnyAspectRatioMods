import { ConfirmModal, DialogCheckbox } from "@decky/ui";
import { useState } from "react";
import type { SupportedGame } from "../../services/types";

export function GameChoiceModal({
  games,
  onChoose,
  onCancel,
}: {
  games: SupportedGame[];
  onChoose: (game: SupportedGame) => void;
  onCancel: () => void;
}) {
  const [selectedIndex, setSelectedIndex] = useState<number | null>(null);

  return (
    <ConfirmModal
      strTitle={`Select the game for ${games[0].app_name}`}
      strDescription="Several fixes match this executable. Select exactly one game."
      strOKButtonText="Confirm"
      strCancelButtonText="Cancel"
      bOKDisabled={selectedIndex === null}
      onOK={() => {
        if (selectedIndex !== null) onChoose(games[selectedIndex]);
      }}
      onCancel={onCancel}
      onEscKeypress={onCancel}
    >
      {games.map((game, index) => (
        <DialogCheckbox
          key={`${game.mod_id}-${game.game_path}`}
          label={game.mod_name}
          description={game.game_path ?? "Unknown game folder"}
          checked={selectedIndex === index}
          controlled
          onChange={(checked) => setSelectedIndex(checked ? index : null)}
        />
      ))}
    </ConfirmModal>
  );
}
