// The launcher's whole state, as the native side (launcher/src/app.cpp) sends it. The UI only draws this and sends messages back.

export type Phase = 'boot' | 'setup' | 'signin' | 'home';

export interface ErrorInfo {
  code: string;
  message: string;
}

export interface StepState {
  id: string;
  label: string;
  status: 'pending' | 'active' | 'done' | 'error' | 'skipped';
  detail: string;
}

export interface Account {
  name: string;
  uuid: string;
  owns: boolean;
  hasProfile: boolean;
  /** The player's head as a data: address (a small PNG); empty when there is no skin to show. */
  avatar: string;
}

export interface LauncherState {
  phase: Phase;
  app: { version: string; mock: boolean; gameVersion: string; clientBundled: boolean; gameDir: string; platform: 'windows' | 'linux' | 'macos' };
  setup: { steps: StepState[]; error: ErrorInfo | null };
  auth: {
    configured: boolean;
    /** waiting: the Microsoft sign-in window is open and the launcher is waiting for the player to finish in it. */
    status: 'signedOut' | 'working' | 'waiting' | 'signedIn';
    message: string;
    error: ErrorInfo | null;
  };
  account: Account | null;
  game: {
    status: 'unknown' | 'missing' | 'ready' | 'working' | 'running';
    phase: string; // client | account | version | extract | assets | start
    label: string;
    done: number;
    total: number;
    error: ErrorInfo | null;
    version: string;
    /** Whether the game's exported data tables (which singleplayer reads) were found on this PC. */
    data: boolean;
  };
  settings: { keepOpen: boolean };
  /** The Mods tab: the folder, the game version mods are searched for, the mods that are installed (by Modrinth project id) and the ones being installed now (with what is going on). */
  mods: { dir: string; gameVersion: string; installed: Record<string, InstalledMod>; busy: Record<string, string>; blocked: BlockedMod[] };
}

/** A file in the mods folder that is an optimization mod (or needs one): Falcon does not support those, whoever put them there. */
export interface BlockedMod {
  file: string;
  name: string;
  reason: string;
}

export interface InstalledMod {
  title: string;
  version: string;
  file: string;
  loader: string;
}

/** One mod of a search on Modrinth. `icon` is a data: address (empty when there is none): the page may not load pictures from the network itself. */
export interface ModHit {
  projectId: string;
  slug: string;
  title: string;
  description: string;
  author: string;
  icon: string;
  downloads: number;
  categories: string[];
  /** An optimization mod (Sodium and the like) or one that needs one: not compatible with Falcon, never will be, so it is not installed. */
  blocked?: boolean;
  blockedReason?: string;
}

export interface ModSearchResult {
  hits: ModHit[];
  offset: number;
  limit: number;
  total: number;
  query: string;
  /** Set when a newer search replaced this one: the page ignores the answer. */
  superseded?: boolean;
}

