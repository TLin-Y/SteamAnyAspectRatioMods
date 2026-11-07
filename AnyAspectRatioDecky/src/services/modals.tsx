import { ConfirmModal, showModal } from "@decky/ui";
import type { LaunchOptionsDisplay, SupportedGame } from "./types";
import { LaunchOptionsInfoModal } from "../components/modals/LaunchOptionsInfoModal";
import { GameChoiceModal } from "../components/modals/GameChoiceModal";

export function confirmFileReplacement(
  gameName: string,
  conflictingFiles: string[],
): Promise<boolean> {
  return new Promise((resolve) => {
    let settled = false;
    let closeModal: (() => void) | undefined;
    const finish = (replaceIni: boolean) => {
      if (settled) return;
      settled = true;
      resolve(replaceIni);
      closeModal?.();
    };

    const modal = showModal(
      <ConfirmModal
        strTitle="Existing mod files differ"
        strDescription={`${gameName} has files that differ from this mod's source:\n${conflictingFiles.join("\n")}\n\nReplace these files? Choosing "Keep existing files" preserves them and still applies the other mod files.`}
        strOKButtonText="Replace files"
        strCancelButtonText="Keep existing files"
        onOK={() => finish(true)}
        onCancel={() => finish(false)}
        onEscKeypress={() => finish(false)}
      />,
      undefined,
      { fnOnClose: () => finish(false) },
    );
    closeModal = modal.Close;
  });
}

export function confirmOfflineMod(gameName: string): Promise<boolean> {
  return new Promise((resolve) => {
    let settled = false;
    let closeModal: (() => void) | undefined;
    const finish = (confirmed: boolean) => {
      if (settled) return;
      settled = true;
      resolve(confirmed);
      closeModal?.();
    };
    const modal = showModal(
      <ConfirmModal
        strTitle="Offline play only"
        strDescription={`${gameName}'s mod must only be used offline. Do not play online with mods active; this can result in a ban. Close the game before applying or removing the mod. Continue?`}
        strOKButtonText="Apply offline mod"
        strCancelButtonText="Cancel"
        onOK={() => finish(true)}
        onCancel={() => finish(false)}
        onEscKeypress={() => finish(false)}
      />,
      undefined,
      { fnOnClose: () => finish(false) },
    );
    closeModal = modal.Close;
  });
}

export function confirmModifiedIniRemoval(
  gameName: string,
  iniFile: string,
): Promise<boolean> {
  return new Promise((resolve) => {
    let settled = false;
    let closeModal: (() => void) | undefined;
    const finish = (keepIni: boolean) => {
      if (settled) return;
      settled = true;
      resolve(keepIni);
      closeModal?.();
    };

    const modal = showModal(
      <ConfirmModal
        strTitle="Keep modified configuration?"
        strDescription={`${gameName}'s ${iniFile} differs from the mod's original configuration. Keep it when removing the mod files?`}
        strOKButtonText="Keep configuration"
        strCancelButtonText="Delete configuration"
        onOK={() => finish(true)}
        onCancel={() => finish(false)}
        onEscKeypress={() => finish(true)}
      />,
      undefined,
      { fnOnClose: () => finish(true) },
    );
    closeModal = modal.Close;
  });
}

export async function copyToClipboard(value: string): Promise<void> {
  try {
    if (!navigator.clipboard?.writeText) {
      throw new Error("Clipboard API is unavailable.");
    }
    await navigator.clipboard.writeText(value);
    return;
  } catch {
    const textarea = document.createElement("textarea");
    textarea.value = value;
    textarea.setAttribute("readonly", "");
    textarea.style.position = "fixed";
    textarea.style.opacity = "0";
    document.body.appendChild(textarea);
    let copied = false;
    try {
      textarea.select();
      copied = document.execCommand("copy");
    } finally {
      textarea.remove();
    }
    if (!copied) throw new Error("Could not copy to the clipboard.");
  }
}

export function showErrorModal(title: string, message: string) {
  showModal(
    <ConfirmModal
      bAlertDialog
      strTitle={title}
      strDescription={message}
      strOKButtonText="OK"
    />,
  );
}

export function showLaunchOptionsInfo(
  title: string,
  message: string,
  values: LaunchOptionsDisplay[],
): void {
  let closeModal = () => {};
  const modal = showModal(
    <LaunchOptionsInfoModal
      title={title}
      message={message}
      values={values}
      closeModal={() => closeModal()}
    />,
  );
  closeModal = modal.Close;
}

export function isEditableIniLine(line: string): boolean {
  const trimmed = line.trim();
  return (
    trimmed.length > 0 &&
    !/^\[[^\]]*\]\s*(?:[;#].*)?$/.test(trimmed) &&
    !/^[;#]/.test(trimmed)
  );
}

export function chooseGame(games: SupportedGame[]): Promise<SupportedGame | null> {
  return new Promise((resolve) => {
    let settled = false;
    let closeModal: (() => void) | undefined;
    const finish = (game: SupportedGame | null) => {
      if (settled) return;
      settled = true;
      resolve(game);
      closeModal?.();
    };
    const modal = showModal(
      <GameChoiceModal games={games} onChoose={finish} onCancel={() => finish(null)} />,
      undefined,
      { fnOnClose: () => finish(null) },
    );
    closeModal = modal.Close;
  });
}
