/** `drop_coin` -> "Drop coin", `openDoor` -> "Open door". */
export function commandLabel(name: string): string {
  const words = name
    .replace(/([a-z0-9])([A-Z])/g, '$1 $2')
    .split(/[_\s-]+/)
    .filter((w) => w.length > 0)
    .map((w) => w.toLowerCase());
  const text = words.join(' ');
  return text.length === 0 ? name : text.charAt(0).toUpperCase() + text.slice(1);
}
