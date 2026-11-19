// Replaces AnyAspectRatioDecky/src/launch_options/steamLaunchOptions.ts in the desktop build.
// The desktop backend puts the overrides where the OS needs them (a macOS Wine bottle's registry),
// or explains how to set them in Steam (Linux).
import { launchAction } from "./decky-api";

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

export function readCurrentLaunchOptions(appId: number): Promise<string> {
  return launchAction("read", [appId]);
}

export async function prepareLaunchOptions(
  appId: number,
  modId: string,
  requiredGroups: string[],
): Promise<LaunchOptionsPlan> {
  const plan = await launchAction<{ currentOptions: string; proposedOptions: string }>(
    "prepare", [appId, modId, requiredGroups],
  );
  return { appId, modId, ...plan, canWrite: true };
}

export function applyLaunchOptions(
  plan: LaunchOptionsPlan,
  requiredGroups: string[],
): Promise<LaunchOptionsUpdateResult> {
  return launchAction("apply", [plan.appId, plan.modId, requiredGroups]);
}

export function removeLaunchOptions(appId: number, modId: string): Promise<LaunchOptionsRemovalResult> {
  return launchAction("remove", [appId, modId]);
}
