type LaunchToken = {
  raw: string;
  value: string;
};

type InjectionRecord = {
  originalOptions: string;
  addedGroups: string[];
  createdAssignment: boolean;
  commandTokenAdded: boolean;
};

type LaunchOptionsState = {
  version: 1;
  records: Record<string, InjectionRecord>;
};

type SteamAppDetails = {
  strLaunchOptions?: string;
};

type SteamApps = {
  RegisterForAppDetails?: (
    appId: number,
    callback: (details: SteamAppDetails) => void,
  ) => { unregister: () => void };
  SetAppLaunchOptions?: (appId: number, options: string) => void;
};

export type LaunchOptionsPlan = {
  appId: number;
  modId: string;
  currentOptions: string;
  proposedOptions: string;
  canWrite: boolean;
};

export type LaunchOptionsUpdateResult = {
  currentOptions: string;
  updatedOptions: string;
};

export type LaunchOptionsRemovalResult = {
  hadRecord: boolean;
  changed: boolean;
  currentOptions: string;
  updatedOptions: string;
};

const STORAGE_KEY = "anyaspectratio.launch-options.v1";
const COMMAND_TOKEN = "%command%";

function getApps(): {
  RegisterForAppDetails?: SteamApps["RegisterForAppDetails"];
  SetAppLaunchOptions?: SteamApps["SetAppLaunchOptions"];
} {
  return (
    globalThis as typeof globalThis & {
      SteamClient?: {
        Apps?: SteamApps;
      };
    }
  ).SteamClient?.Apps ?? {};
}

function tokenize(options: string): LaunchToken[] {
  const tokens: LaunchToken[] = [];
  let start = -1;
  let quote: "'" | '"' | null = null;
  let escaped = false;

  const push = (end: number) => {
    if (start < 0) return;
    const raw = options.slice(start, end);
    let value = "";
    let tokenQuote: "'" | '"' | null = null;
    for (let index = 0; index < raw.length; index += 1) {
      const character = raw[index];
      if (character === "\\" && tokenQuote !== "'" && index + 1 < raw.length) {
        value += raw[index + 1];
        index += 1;
      } else if (tokenQuote) {
        if (character === tokenQuote) tokenQuote = null;
        else value += character;
      } else if (character === "'" || character === '"') {
        tokenQuote = character;
      } else {
        value += character;
      }
    }
    tokens.push({ raw, value });
    start = -1;
  };

  for (let index = 0; index < options.length; index += 1) {
    const character = options[index];
    if (start < 0) {
      if (/\s/.test(character)) continue;
      start = index;
    }
    if (escaped) escaped = false;
    else if (character === "\\" && quote !== "'") escaped = true;
    else if (quote) {
      if (character === quote) quote = null;
    } else if (character === "'" || character === '"') {
      quote = character;
    } else if (/\s/.test(character)) {
      push(index);
    }
  }
  if (quote || escaped) {
    throw new Error(
      "Current Steam launch options contain incomplete quoting and cannot be changed safely.",
    );
  }
  push(options.length);
  return tokens;
}

function serialize(tokens: LaunchToken[]): string {
  return tokens.map(({ raw }) => raw).join(" ");
}

function isAssignment(token: LaunchToken): boolean {
  return /^[A-Za-z_][A-Za-z0-9_]*=/.test(token.value);
}

function assignmentPrefixLength(tokens: LaunchToken[]): number {
  let index = 0;
  while (index < tokens.length && isAssignment(tokens[index])) index += 1;
  return index;
}

function encodeOverrideAssignment(value: string): string {
  return `WINEDLLOVERRIDES="${value.replace(/(["\\$`])/g, "\\$1")}"`;
}

function splitOverrideAssignment(
  token: LaunchToken,
): { value: string } | null {
  const match = /^WINEDLLOVERRIDES=(.*)$/i.exec(token.value);
  return match ? { value: match[1] } : null;
}

function parseOverrideRule(group: string): [string[], string] | null {
  const separator = group.indexOf("=");
  if (separator <= 0 || separator === group.length - 1) return null;
  const dlls = group.slice(0, separator).split(",");
  const rule = group.slice(separator + 1).toLowerCase();
  if (!dlls.every((dll) => /^[a-z0-9_]+$/i.test(dll))) return null;
  return [dlls.map((dll) => dll.toLowerCase()), rule];
}

function getOverrideRules(groups: string[]): Map<string, string> {
  const rules = new Map<string, string>();
  for (const group of groups) {
    const parsed = parseOverrideRule(group);
    if (!parsed) continue;
    for (const dll of parsed[0]) rules.set(dll, parsed[1]);
  }
  return rules;
}

function mergeOverrides(
  options: string,
  requiredGroups: string[],
): {
  options: string;
  addedGroups: string[];
  createdAssignment: boolean;
  commandTokenAdded: boolean;
} {
  const tokens = tokenize(options);
  const prefixLength = assignmentPrefixLength(tokens);
  let overrideIndex = -1;
  let currentGroups: string[] = [];

  for (let index = 0; index < prefixLength; index += 1) {
    const assignment = splitOverrideAssignment(tokens[index]);
    if (assignment) {
      overrideIndex = index;
      currentGroups = assignment.value
        .split(";")
        .map((group) => group.trim())
        .filter(Boolean);
    }
  }

  const currentRules = getOverrideRules(currentGroups);
  const addedGroups: string[] = [];
  for (const group of requiredGroups) {
    const required = parseOverrideRule(group);
    if (!required) throw new Error(`Invalid catalogued Wine DLL override: ${group}`);
    const missingDlls = required[0].filter(
      (dll) => currentRules.get(dll) !== required[1],
    );
    if (missingDlls.length === 0) continue;
    const addedGroup = `${missingDlls.join(",")}=${required[1]}`;
    addedGroups.push(addedGroup);
    currentGroups.push(addedGroup);
    for (const dll of missingDlls) currentRules.set(dll, required[1]);
  }
  if (addedGroups.length === 0) {
    return {
      options: serialize(tokens),
      addedGroups,
      createdAssignment: false,
      commandTokenAdded: false,
    };
  }

  const mergedValue = currentGroups.join(";");
  const createdAssignment = overrideIndex < 0;
  if (createdAssignment) {
    tokens.splice(prefixLength, 0, {
      raw: encodeOverrideAssignment(mergedValue),
      value: `WINEDLLOVERRIDES=${mergedValue}`,
    });
  } else {
    tokens[overrideIndex] = {
      raw: encodeOverrideAssignment(mergedValue),
      value: `WINEDLLOVERRIDES=${mergedValue}`,
    };
  }

  const commandTokenAdded = !tokens.some(
    (token) => token.value.toLowerCase() === COMMAND_TOKEN,
  );
  if (commandTokenAdded) {
    const updatedPrefixLength = assignmentPrefixLength(tokens);
    tokens.splice(updatedPrefixLength, 0, {
      raw: COMMAND_TOKEN,
      value: COMMAND_TOKEN,
    });
  }
  return {
    options: serialize(tokens),
    addedGroups,
    createdAssignment,
    commandTokenAdded,
  };
}

function stateKey(appId: number, modId: string): string {
  return `${appId}:${modId}`;
}

function readState(): LaunchOptionsState {
  const raw = window.localStorage.getItem(STORAGE_KEY);
  if (raw === null) return { version: 1, records: {} };
  const parsed: unknown = JSON.parse(raw);
  if (
    !parsed ||
    typeof parsed !== "object" ||
    !("version" in parsed) ||
    parsed.version !== 1 ||
    !("records" in parsed) ||
    !parsed.records ||
    typeof parsed.records !== "object" ||
    Array.isArray(parsed.records)
  ) {
    throw new Error("Saved launch-option ownership data is invalid.");
  }
  const records = parsed.records as Record<string, unknown>;
  for (const record of Object.values(records)) {
    if (
      !record ||
      typeof record !== "object" ||
      !("originalOptions" in record) ||
      typeof record.originalOptions !== "string" ||
      !("addedGroups" in record) ||
      !Array.isArray(record.addedGroups) ||
      !record.addedGroups.every((group) => typeof group === "string") ||
      !("createdAssignment" in record) ||
      typeof record.createdAssignment !== "boolean" ||
      !("commandTokenAdded" in record) ||
      typeof record.commandTokenAdded !== "boolean"
    ) {
      throw new Error("Saved launch-option ownership data is invalid.");
    }
  }
  return parsed as LaunchOptionsState;
}

function writeState(state: LaunchOptionsState): void {
  window.localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
}

export async function readCurrentLaunchOptions(appId: number): Promise<string> {
  if (!Number.isSafeInteger(appId) || appId <= 0) {
    throw new Error("The catalogued Steam App ID is invalid.");
  }
  const apps = getApps();
  if (!apps.RegisterForAppDetails) {
    throw new Error(
      "Steam's launch-options read API is unavailable. Add the required WINEDLLOVERRIDES manually in the game's Steam Properties.",
    );
  }
  return new Promise((resolve, reject) => {
    let active = true;
    let registration: { unregister: () => void } | undefined;
    const timer = window.setTimeout(() => {
      finish(new Error("Timed out waiting for Steam app details."));
    }, 5000);
    const unsubscribe = () => {
      active = false;
      registration?.unregister();
    };
    const finish = (error?: Error, options?: string) => {
      if (!active) return;
      window.clearTimeout(timer);
      unsubscribe();
      if (error) reject(error);
      else if (typeof options === "string") resolve(options);
      else reject(new Error("Steam app details did not include launch options."));
    };
    try {
      registration = apps.RegisterForAppDetails!(appId, (details) => {
        finish(undefined, details?.strLaunchOptions ?? "");
      });
      if (!active) registration.unregister();
    } catch (error) {
      finish(
        error instanceof Error ? error : new Error(String(error)),
      );
    }
  });
}

async function waitForLaunchOptions(
  appId: number,
  expectedOptions: string,
): Promise<void> {
  const deadline = Date.now() + 5000;
  while (Date.now() <= deadline) {
    const currentOptions = await readCurrentLaunchOptions(appId);
    if (
      serialize(tokenize(currentOptions)) ===
      serialize(tokenize(expectedOptions))
    ) {
      return;
    }
    if (Date.now() < deadline) {
      await new Promise((resolve) => window.setTimeout(resolve, 100));
    }
  }
  throw new Error("Steam did not confirm the launch-options update.");
}

async function writeLaunchOptions(
  appId: number,
  options: string,
  previousOptions: string,
): Promise<void> {
  const apps = getApps();
  if (!apps.SetAppLaunchOptions) {
    throw new Error(
      "Steam's launch-options write API is unavailable. Add the required WINEDLLOVERRIDES manually in the game's Steam Properties.",
    );
  }
  try {
    apps.SetAppLaunchOptions(appId, options);
    await waitForLaunchOptions(appId, options);
  } catch (error) {
    const failure = error instanceof Error ? error : new Error(String(error));
    try {
      apps.SetAppLaunchOptions(appId, previousOptions);
      await waitForLaunchOptions(appId, previousOptions);
    } catch (rollbackError) {
      const rollback =
        rollbackError instanceof Error
          ? rollbackError
          : new Error(String(rollbackError));
      throw new Error(
        `${failure.message}; restoring the previous launch options also failed: ${rollback.message}`,
      );
    }
    throw failure;
  }
}

export async function prepareLaunchOptions(
  appId: number,
  modId: string,
  requiredGroups: string[],
): Promise<LaunchOptionsPlan> {
  readState();
  const currentOptions = await readCurrentLaunchOptions(appId);
  const merged = mergeOverrides(currentOptions, requiredGroups);
  return {
    appId,
    modId,
    currentOptions,
    proposedOptions: merged.options,
    canWrite:
      merged.options === currentOptions ||
      typeof getApps().SetAppLaunchOptions === "function",
  };
}

export async function applyLaunchOptions(
  plan: LaunchOptionsPlan,
  requiredGroups: string[],
): Promise<LaunchOptionsUpdateResult> {
  const currentOptions = await readCurrentLaunchOptions(plan.appId);
  const merged = mergeOverrides(currentOptions, requiredGroups);
  const state = readState();
  const key = stateKey(plan.appId, plan.modId);
  const existing = state.records[key];
  const addedGroups = [
    ...new Set([...(existing?.addedGroups ?? []), ...merged.addedGroups]),
  ];

  if (merged.options === currentOptions && !existing) {
    return {
      currentOptions,
      updatedOptions: merged.options,
    };
  }
  if (merged.options !== currentOptions) {
    await writeLaunchOptions(plan.appId, merged.options, currentOptions);
  }
  if (addedGroups.length > 0 || existing) {
    state.records[key] = {
      originalOptions: existing?.originalOptions ?? plan.currentOptions,
      addedGroups,
      createdAssignment:
        (existing?.createdAssignment ?? false) || merged.createdAssignment,
      commandTokenAdded:
        (existing?.commandTokenAdded ?? false) || merged.commandTokenAdded,
    };
    try {
      writeState(state);
    } catch (error) {
      if (merged.options !== currentOptions) {
        await writeLaunchOptions(plan.appId, currentOptions, merged.options);
      }
      throw error;
    }
  }
  return {
    currentOptions,
    updatedOptions: merged.options,
  };
}

function removeOwnedOverrides(
  options: string,
  record: InjectionRecord,
): { options: string; changed: boolean } {
  const tokens = tokenize(options);
  const prefixLength = assignmentPrefixLength(tokens);
  let overrideIndex = -1;
  for (let index = prefixLength - 1; index >= 0; index -= 1) {
    if (splitOverrideAssignment(tokens[index])) {
      overrideIndex = index;
      break;
    }
  }

  let changed = false;
  if (overrideIndex >= 0) {
    const assignment = splitOverrideAssignment(tokens[overrideIndex]);
    if (assignment) {
      const groups = assignment.value
        .split(";")
        .map((group) => group.trim())
        .filter(Boolean);
      for (const addedGroup of record.addedGroups) {
        const index = groups.findIndex(
          (group) => group.toLowerCase() === addedGroup.toLowerCase(),
        );
        if (index >= 0) {
          groups.splice(index, 1);
          changed = true;
        }
      }
      if (changed || (!groups.length && record.createdAssignment)) {
        const value = groups.join(";");
        if (!value && record.createdAssignment) {
          tokens.splice(overrideIndex, 1);
          changed = true;
        } else {
          tokens[overrideIndex] = {
            raw: encodeOverrideAssignment(value),
            value: `WINEDLLOVERRIDES=${value}`,
          };
        }
      }
    }
  }

  if (record.commandTokenAdded) {
    const currentPrefixLength = assignmentPrefixLength(tokens);
    if (
      tokens[currentPrefixLength]?.value.toLowerCase() === COMMAND_TOKEN
    ) {
      const withoutCommand = [...tokens];
      withoutCommand.splice(currentPrefixLength, 1);
      const currentValues = withoutCommand.map((token) => token.value);
      const originalValues = tokenize(record.originalOptions).map(
        (token) => token.value,
      );
      if (
        currentValues.length === originalValues.length &&
        currentValues.every((value, index) => value === originalValues[index])
      ) {
        tokens.splice(currentPrefixLength, 1);
        changed = true;
      }
    }
  }
  return { options: serialize(tokens), changed };
}

export async function removeLaunchOptions(
  appId: number,
  modId: string,
): Promise<LaunchOptionsRemovalResult> {
  const state = readState();
  const key = stateKey(appId, modId);
  const record = state.records[key];
  const currentOptions = await readCurrentLaunchOptions(appId);
  if (!record) {
    return {
      hadRecord: false,
      changed: false,
      currentOptions,
      updatedOptions: currentOptions,
    };
  }
  const removal = removeOwnedOverrides(currentOptions, record);
  if (removal.options !== currentOptions) {
    await writeLaunchOptions(appId, removal.options, currentOptions);
  }
  delete state.records[key];
  writeState(state);
  return {
    hadRecord: true,
    changed: removal.changed,
    currentOptions,
    updatedOptions: removal.options,
  };
}
