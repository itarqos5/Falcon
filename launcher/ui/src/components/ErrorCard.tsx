import { useState } from 'react';
import { call } from '../bridge';
import { describeError } from '../format';
import { AlertCircle, Copy, External } from '../icons';

interface ErrorCardProps {
  error?: { code?: string; message?: string } | null;
  onRetry?: () => void;
  retryLabel?: string;
}

/** A problem, said plainly: what happened, and the button that fixes it when there is one. */
export function ErrorCard({ error, onRetry, retryLabel = 'Try again' }: ErrorCardProps) {
  const [copied, setCopied] = useState(false);
  if (!error) return null;
  const meta = describeError(error.code, error.message);

  const copyDetails = () => {
    const text = `Falcon Launcher error [${error.code || 'unknown'}]: ${error.message || meta.description}`;
    if (navigator.clipboard?.writeText) void navigator.clipboard.writeText(text).catch(() => undefined);
    setCopied(true);
    window.setTimeout(() => setCopied(false), 1800);
  };

  return (
    <div className="admonition error" role="alert">
      <span className="admonition-icon">
        <AlertCircle size={20} />
      </span>
      <div className="admonition-body">
        <div className="admonition-title">{meta.title}</div>
        <p>{meta.description}</p>
        <div className="btn-row">
          {onRetry && (
            <button className="btn small" onClick={onRetry}>
              {retryLabel}
            </button>
          )}
          {meta.action && (
            <button className="btn small" onClick={() => void call('openUrl', { url: meta.action!.url })}>
              <External size={14} /> {meta.action.label}
            </button>
          )}
          <button className="btn small text" onClick={copyDetails}>
            <Copy size={14} /> {copied ? 'Copied' : 'Copy details'}
          </button>
        </div>
      </div>
    </div>
  );
}
