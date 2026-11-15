import { call } from '../bridge';
import { Lock, Microsoft, Spinner } from '../icons';
import { falconLogo } from '../logo';
import type { LauncherState } from '../types';
import { ErrorCard } from './ErrorCard';

export function SignIn({ state }: { state: LauncherState }) {
  const { auth } = state;
  const waiting = auth.status === 'waiting';
  const busy = auth.status === 'working';

  return (
    <main className="page centered">
      <div className="auth-stack">
        <section className="card auth-card">
          <img className="auth-logo" src={falconLogo} alt="" width={56} height={56} draggable={false} />
          {waiting ? (
            <>
              <h1>Finish in the Microsoft window</h1>
              <p className="muted">Sign in with the account you play Minecraft with. This page continues by itself.</p>
              <div className="auth-wait">
                <Spinner size={18} /> Waiting for Microsoft…
              </div>
              <button className="btn" onClick={() => void call('cancelSignIn')}>
                Cancel
              </button>
            </>
          ) : (
            <>
              <h1>Sign in to Falcon</h1>
              <p className="muted">Falcon needs a Microsoft account that owns Minecraft: Java Edition. A Microsoft window opens for you to sign in; Falcon never sees your password.</p>
              <button className="btn microsoft" disabled={busy || !auth.configured} onClick={() => void call('signIn')}>
                {busy ? <Spinner size={18} /> : <Microsoft size={18} />}
                {busy ? auth.message || 'Signing you in…' : 'Sign in with Microsoft'}
              </button>
              {!busy && auth.message && <div className="auth-note">{auth.message}</div>}
              <div className="auth-fine">
                <Lock size={13} /> {state.app.platform === 'linux' ? 'Your sign-in is kept in a file that only your user can read.' : 'Your sign-in is stored encrypted for your Windows user only.'}
              </div>
            </>
          )}
        </section>
        {auth.error && <ErrorCard error={auth.error} onRetry={auth.configured ? () => void call('signIn') : undefined} retryLabel="Try again" />}
      </div>
    </main>
  );
}
