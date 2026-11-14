import { call } from '../bridge';
import { Check, Cross, Spinner } from '../icons';
import { falconLogo } from '../logo';
import type { LauncherState, StepState } from '../types';
import { ErrorCard } from './ErrorCard';

function StepIcon({ status }: { status: StepState['status'] }) {
  if (status === 'done') return <span className="step-icon done"><Check size={12} /></span>;
  if (status === 'error') return <span className="step-icon error"><Cross size={12} /></span>;
  if (status === 'active') return <span className="step-icon active"><Spinner size={16} /></span>;
  if (status === 'skipped') return <span className="step-icon skipped">–</span>;
  return <span className="step-icon pending" />;
}

export function Setup({ state }: { state: LauncherState }) {
  const steps = state.setup.steps;
  const finished = steps.filter((s) => s.status === 'done' || s.status === 'skipped').length;
  const percent = steps.length ? Math.round((finished / steps.length) * 100) : 0;
  const error = state.setup.error;

  return (
    <main className="page centered">
      <div className="auth-stack wide">
        <section className="card setup-card">
          <div className="setup-head">
            <img src={falconLogo} alt="" width={44} height={44} draggable={false} />
            <div>
              <h1>Setting up Falcon</h1>
              <p className="muted">{error ? 'Fix what is below and try again.' : 'This only happens once and takes a few seconds.'}</p>
            </div>
          </div>
          <div className="bar" role="progressbar" aria-valuenow={percent} aria-valuemin={0} aria-valuemax={100}>
            <div className="bar-fill" style={{ width: `${percent}%` }} />
          </div>
          <ul className="steps">
            {steps.map((step) => (
              <li key={step.id} className={`step ${step.status}`}>
                <StepIcon status={step.status} />
                <div className="step-text">
                  <div className="step-label">{step.label}</div>
                  {step.detail && <div className="step-detail">{step.detail}</div>}
                </div>
              </li>
            ))}
          </ul>
        </section>
        {error && <ErrorCard error={error} onRetry={() => void call('retrySetup')} retryLabel="Retry setup" />}
      </div>
    </main>
  );
}
