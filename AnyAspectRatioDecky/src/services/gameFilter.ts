import type { SupportedGame } from "./types";

const words = (text: string) => text.toLowerCase().split(/\s+/).filter(Boolean);

/** Every word of the query must appear in the game's or the fix's name, ignoring case. */
export function gameMatchesFilter(game: SupportedGame, query: string): boolean {
  const wanted = words(query);
  if (wanted.length === 0) return true;
  const haystack = `${game.app_name} ${game.mod_name}`.toLowerCase();
  return wanted.every((word) => haystack.includes(word));
}
