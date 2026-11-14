import type { LauncherState } from './types';

// A stand-in for the launcher, for working on the pages in a browser (npm run dev, then open .../#setup, #signin, #waiting, #home, #missing,
// #notowned, #noprofile, #installing, #running or #error). Nothing here ships in the launcher: it is only loaded when there is no host.

/** A blocky face drawn on a canvas, like the one the launcher cuts out of a skin. */
function devAvatar(): string {
  const palette: Record<string, string> = { H: '#462b1a', h: '#603c23', S: '#c68662', s: '#b07252', W: '#fafafa', B: '#403c96', M: '#78463c', D: '#5e3a28' };
  const rows = ['HhHHHHhH', 'HHHhHHHH', 'HSSSSSSH', 'SSSSSSSS', 'SWBSSBWS', 'SSSssSSS', 'SDMMMMDS', 'SDDDDDDS'];
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 96;
  const context = canvas.getContext('2d');
  if (!context) return '';
  rows.forEach((row, y) => [...row].forEach((c, x) => {
    context.fillStyle = palette[c];
    context.fillRect(x * 12, y * 12, 12, 12);
  }));
  return canvas.toDataURL('image/png');
}

const base = (): LauncherState => ({
  phase: 'home',
  app: { version: '0.1.0', mock: true, gameVersion: '26.3', clientBundled: true, gameDir: 'C:/Users/you/AppData/Local/Falcon/game/26.3' },
  setup: { steps: [], error: null },
  auth: { configured: true, status: 'signedIn', message: '', error: null },
  account: { name: 'FalconPlayer', uuid: '069a79f444e94726a5befca90e38aaf5', owns: true, hasProfile: true, avatar: devAvatar() },
  game: { status: 'ready', phase: '', label: '', done: 0, total: 0, error: null, version: '26.3', data: true },
  settings: { keepOpen: true },
});

function stateFor(name: string): LauncherState {
  const s = base();
  switch (name) {
    case 'setup':
      s.phase = 'setup';
      s.setup.steps = [
        { id: 'runtime', label: 'Checking the web runtime', status: 'done', detail: 'Microsoft Edge WebView2 is installed' },
        { id: 'folders', label: "Creating the launcher's folders", status: 'done', detail: 'C:\\Users\\you\\AppData\\Local\\Falcon' },
        { id: 'client', label: 'Installing the Falcon client', status: 'active', detail: '' },
        { id: 'shortcut', label: 'Adding a Start menu shortcut', status: 'pending', detail: '' },
        { id: 'finish', label: 'Finishing up', status: 'pending', detail: '' },
      ];
      break;
    case 'signin':
    case 'waiting':
    case 'device':
    case 'error':
      s.phase = 'signin';
      s.account = null;
      s.auth.status = name === 'waiting' || name === 'device' ? 'waiting' : 'signedOut';
      if (s.auth.status === 'waiting') s.auth.message = 'Finish signing in in the Microsoft window.';
      if (name === 'error') s.auth.error = { code: 'xbox_no_account', message: 'This Microsoft account has no Xbox profile yet. Sign in once at xbox.com to create one, then try again.' };
      break;
    case 'nodata':
      s.game.data = false;
      break;
    case 'missing':
      s.game.status = 'missing';
      break;
    case 'notowned':
      s.account!.owns = false;
      break;
    case 'noprofile':
      s.account!.hasProfile = false;
      break;
    case 'installing':
      s.game = { ...s.game, status: 'working', phase: 'assets', label: 'Downloading sounds and languages', done: 151_000_000, total: 360_000_000 };
      break;
    case 'running':
      s.game = { ...s.game, status: 'running', label: 'Falcon is running' };
      break;
    default:
      break;
  }
  return s;
}

export function createDevHost(deliver: (message: unknown) => void): { send: (message: string) => void } {
  return {
    send(message: string) {
      const request = JSON.parse(message) as { id: number; method: string; params?: Record<string, unknown> };
      console.info('[dev host]', request.method, request.params);
      if (request.method === 'uiReady') {
        deliver({ event: 'state', state: stateFor(window.location.hash.replace('#', '')) });
      }
      deliver({ id: request.id, ok: true, result: {} });
    },
  };
}
