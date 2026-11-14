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
  app: { version: string; mock: boolean; gameVersion: string; clientBundled: boolean; gameDir: string };
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
}
