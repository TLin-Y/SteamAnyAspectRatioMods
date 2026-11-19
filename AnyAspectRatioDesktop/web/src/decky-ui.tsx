import {
  cloneElement,
  isValidElement,
  useEffect,
  useRef,
  useState,
  useSyncExternalStore,
  type CSSProperties,
  type ChangeEvent,
  type MouseEvent,
  type ReactElement,
  type ReactNode,
} from "react";
import { openUrl } from "./decky-api";

export const staticClasses = { Title: "aar-title" };

export const Navigation = {
  NavigateToExternalWeb: (url: string) => void openUrl(url),
};

export function PanelSection({ title, children }: { title?: ReactNode; children?: ReactNode }) {
  return (
    <section className="aar-section">
      {title && <h2 className="aar-section-title">{title}</h2>}
      {children}
    </section>
  );
}

export function PanelSectionRow({ children }: { children?: ReactNode }) {
  return <div className="aar-row">{children}</div>;
}

type ButtonProps = {
  children?: ReactNode;
  onClick?: (event: MouseEvent) => void;
  disabled?: boolean;
  style?: CSSProperties;
  className?: string;
};

export function DialogButton({ children, onClick, disabled, style, className }: ButtonProps) {
  return (
    <button
      type="button"
      className={`aar-button ${className ?? ""}`}
      style={style}
      disabled={disabled}
      onClick={onClick}
    >
      {children}
    </button>
  );
}

export function ButtonItem({
  children, onClick, disabled, label, description,
}: ButtonProps & { layout?: "below" | "inline"; label?: ReactNode; description?: ReactNode }) {
  return (
    <div className="aar-field">
      {label && <div className="aar-field-label">{label}</div>}
      <DialogButton className="aar-button-wide" onClick={onClick} disabled={disabled}>{children}</DialogButton>
      {description && <div className="aar-field-description">{description}</div>}
    </div>
  );
}

export function ToggleField({
  label, description, checked, disabled, onChange,
}: {
  label?: ReactNode;
  description?: ReactNode;
  checked: boolean;
  disabled?: boolean;
  onChange?: (checked: boolean) => void;
}) {
  const [value, setValue] = useState(checked);
  useEffect(() => setValue(checked), [checked]);
  return (
    <label className={`aar-toggle ${disabled ? "aar-disabled" : ""}`}>
      <span className="aar-toggle-text">
        <span>{label}</span>
        {description && <span className="aar-field-description">{description}</span>}
      </span>
      <input
        type="checkbox"
        role="switch"
        checked={value}
        disabled={disabled}
        onChange={(event) => {
          setValue(event.currentTarget.checked);
          onChange?.(event.currentTarget.checked);
        }}
      />
      <span className="aar-switch" aria-hidden="true" />
    </label>
  );
}

export function DialogCheckbox({
  label, description, checked, disabled, onChange,
}: {
  label?: ReactNode;
  description?: ReactNode;
  checked?: boolean;
  controlled?: boolean;
  disabled?: boolean;
  onChange?: (checked: boolean) => void;
}) {
  return (
    <label className="aar-checkbox">
      <input
        type="checkbox"
        checked={!!checked}
        disabled={disabled}
        onChange={(event) => onChange?.(event.currentTarget.checked)}
      />
      <span className="aar-toggle-text">
        <span>{label}</span>
        {description && <span className="aar-field-description">{description}</span>}
      </span>
    </label>
  );
}

export function TextField({
  label, value, "aria-label": ariaLabel, onChange, mustBeNumeric, focusOnMount, disabled,
}: {
  label?: ReactNode;
  value?: string;
  "aria-label"?: string;
  onChange?: (event: ChangeEvent<HTMLInputElement>) => void;
  mustBeNumeric?: boolean;
  focusOnMount?: boolean;
  disabled?: boolean;
}) {
  const input = useRef<HTMLInputElement>(null);
  useEffect(() => {
    if (focusOnMount) input.current?.select();
  }, [focusOnMount]);
  return (
    <div className="aar-field">
      {label && <div className="aar-field-label">{label}</div>}
      <input
        ref={input}
        className="aar-input"
        inputMode={mustBeNumeric ? "numeric" : undefined}
        value={value}
        aria-label={ariaLabel}
        placeholder={ariaLabel}
        disabled={disabled}
        onChange={(event) => {
          if (mustBeNumeric && !/^\d*$/.test(event.currentTarget.value)) return;
          onChange?.(event);
        }}
      />
    </div>
  );
}

export function ConfirmModal({
  strTitle, strDescription, strOKButtonText = "OK", strCancelButtonText = "Cancel",
  bAlertDialog, bOKDisabled, onOK, onCancel, onEscKeypress, closeModal, children,
}: {
  strTitle?: ReactNode;
  strDescription?: ReactNode;
  strOKButtonText?: ReactNode;
  strCancelButtonText?: ReactNode;
  bAlertDialog?: boolean;
  bAllowFullSize?: boolean;
  bOKDisabled?: boolean;
  onOK?: () => void;
  onCancel?: () => void;
  onEscKeypress?: () => void;
  closeModal?: () => void;
  children?: ReactNode;
}) {
  const ok = () => {
    onOK?.();
    closeModal?.();
  };
  const cancel = () => {
    onCancel?.();
    closeModal?.();
  };
  const dialog = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      // Only the top dialog answers Escape; the ones under it stay open.
      const top = [...document.querySelectorAll(".aar-modal")].pop();
      if (event.key !== "Escape" || dialog.current !== top) return;
      (onEscKeypress ?? onCancel)?.();
      closeModal?.();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  });
  return (
    <div ref={dialog} className="aar-modal" role="dialog" aria-modal="true">
      {strTitle && <h2 className="aar-modal-title">{strTitle}</h2>}
      {strDescription && <div className="aar-modal-description">{strDescription}</div>}
      {children && <div className="aar-modal-body">{children}</div>}
      <div className="aar-modal-buttons">
        <DialogButton className="aar-button-primary" disabled={bOKDisabled} onClick={ok}>{strOKButtonText}</DialogButton>
        {!bAlertDialog && <DialogButton onClick={cancel}>{strCancelButtonText}</DialogButton>}
      </div>
    </div>
  );
}

type OpenModal = { id: number; element: ReactNode; close: () => void };

let modals: OpenModal[] = [];
let nextModalId = 1;
const listeners = new Set<() => void>();
const publish = () => listeners.forEach((listener) => listener());

export function showModal(
  element: ReactNode,
  _parent?: unknown,
  options?: { fnOnClose?: () => void },
): { Close: () => void; Update: (element: ReactNode) => void } {
  const id = nextModalId++;
  let closed = false;
  const close = () => {
    if (closed) return;
    closed = true;
    modals = modals.filter((modal) => modal.id !== id);
    publish();
    options?.fnOnClose?.();
  };
  const withClose = (node: ReactNode) =>
    isValidElement(node) ? cloneElement(node as ReactElement<{ closeModal?: () => void }>, { closeModal: close }) : node;
  modals = [...modals, { id, element: withClose(element), close }];
  publish();
  return {
    Close: close,
    Update: (next) => {
      modals = modals.map((modal) => (modal.id === id ? { ...modal, element: withClose(next) } : modal));
      publish();
    },
  };
}

export function ModalHost() {
  const open = useSyncExternalStore(
    (listener) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    () => modals,
  );
  return (
    <>
      {open.map((modal) => (
        <div key={modal.id} className="aar-backdrop" onMouseDown={(event) => event.target === event.currentTarget && modal.close()}>
          {modal.element}
        </div>
      ))}
    </>
  );
}
