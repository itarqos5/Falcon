import type { MouseEvent as ReactMouseEvent } from 'react';
import { call } from '../bridge';
import { Cross, Minus } from '../icons';
import { falconLogo } from '../logo';

/**
 * The window has no system title bar: this is it. The empty parts drag the window (the browser reports them to Windows through
 * `app-region: drag`; where the runtime cannot, a mouse press on them asks the launcher to start the drag), the buttons on it do not.
 */
export function TitleBar({ title }: { title?: string }) {
  const startDrag = (event: ReactMouseEvent) => {
    if (event.button === 0 && (event.target as HTMLElement).closest('.no-drag') === null) void call('startDrag');
  };
  return (
    <header className="titlebar" onMouseDown={startDrag}>
      <div className="titlebar-brand">
        <img src={falconLogo} alt="" width={18} height={18} draggable={false} />
        <span>Falcon</span>
        {title && <span className="titlebar-crumb">{title}</span>}
      </div>
      <div className="win-controls no-drag">
        <button className="win-btn" aria-label="Minimize" title="Minimize" onClick={() => void call('minimize')}>
          <Minus size={14} />
        </button>
        <button className="win-btn close" aria-label="Close" title="Close" onClick={() => void call('close')}>
          <Cross size={14} />
        </button>
      </div>
    </header>
  );
}
