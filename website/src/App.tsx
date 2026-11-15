import { useEffect, useRef, useState, type ReactNode } from 'react';
import AnimatedContent from './components/reactbits/AnimatedContent';
import CountUp from './components/reactbits/CountUp';
import DotGrid from './components/reactbits/DotGrid';
import ShinyText from './components/reactbits/ShinyText';
import SpotlightCard from './components/reactbits/SpotlightCard';
import { RELEASE, REPO, VERSION, caveats, faq, features, memory, memoryRatio, missing, shots, startup, startupRatio } from './data';

// ---- small helpers -------------------------------------------------------------------------------------------------------------

function useReducedMotion(): boolean {
  const [reduced, setReduced] = useState(() => (typeof window !== 'undefined' ? window.matchMedia('(prefers-reduced-motion: reduce)').matches : false));
  useEffect(() => {
    const query = window.matchMedia('(prefers-reduced-motion: reduce)');
    const update = () => setReduced(query.matches);
    query.addEventListener('change', update);
    return () => query.removeEventListener('change', update);
  }, []);
  return reduced;
}

function useInView<T extends Element>(): [React.RefObject<T | null>, boolean] {
  const ref = useRef<T | null>(null);
  const [seen, setSeen] = useState(false);
  useEffect(() => {
    const el = ref.current;
    if (!el || seen) return;
    const observer = new IntersectionObserver(
      (entries) => {
        if (entries.some((e) => e.isIntersecting)) {
          setSeen(true);
          observer.disconnect();
        }
      },
      { threshold: 0.25 },
    );
    observer.observe(el);
    return () => observer.disconnect();
  }, [seen]);
  return [ref, seen];
}

// A section's content fades up as it scrolls into view (React Bits AnimatedContent); nothing moves for people who ask for less motion.
function Reveal({ children, delay = 0 }: { children: ReactNode; delay?: number }) {
  const reduced = useReducedMotion();
  if (reduced) return <>{children}</>;
  return (
    <AnimatedContent distance={28} duration={0.8} ease="power3.out" threshold={0.12} delay={delay}>
      {children}
    </AnimatedContent>
  );
}

// A number that counts up when it comes into view (React Bits CountUp).
function Num({ value, decimals = 0, suffix = '', prefix = '', duration = 1.8 }: { value: number; decimals?: number; suffix?: string; prefix?: string; duration?: number }) {
  const reduced = useReducedMotion();
  if (reduced) return <span>{prefix + value.toFixed(decimals) + suffix}</span>;
  return <CountUp to={value} decimals={decimals} suffix={suffix} prefix={prefix} duration={duration} />;
}

// ---- the chart ------------------------------------------------------------------------------------------------------------------

type BarProps = { label: string; value: number; decimals?: number; suffix: string; scale: number; falcon?: boolean; note?: string };

function Bar({ label, value, decimals = 0, suffix, scale, falcon, note }: BarProps) {
  const [ref, seen] = useInView<HTMLDivElement>();
  const reduced = useReducedMotion();
  return (
    <div className="bar-row" ref={ref}>
      <div className="bar-head">
        <span className="bar-label">{label}</span>
        <span className="bar-value">
          <Num value={value} decimals={decimals} suffix={suffix} />
        </span>
      </div>
      <div className="bar-track" role="img" aria-label={`${label}: ${value.toFixed(decimals)}${suffix}`}>
        <div className={`bar-fill${falcon ? ' falcon' : ''}`} style={{ width: seen || reduced ? `${(value / scale) * 100}%` : '0%' }} />
      </div>
      {note && <p className="bar-note">{note}</p>}
    </div>
  );
}

// ---- the page -------------------------------------------------------------------------------------------------------------------

function Nav() {
  return (
    <header className="nav">
      <div className="wrap nav-inner">
        <a className="brand" href="#top" aria-label="Falcon">
          <img src="/falcon.png" alt="" width="26" height="26" />
          <span>Falcon</span>
        </a>
        <nav aria-label="Main">
          <a href="#benchmarks">Benchmarks</a>
          <a href="#features">Features</a>
          <a href="#gallery">Screenshots</a>
          <a href="#how">How it works</a>
          <a href="#faq">FAQ</a>
        </nav>
        <div className="nav-actions">
          <a className="btn ghost" href={REPO} target="_blank" rel="noreferrer">
            GitHub
          </a>
          <a className="btn primary" href={RELEASE} target="_blank" rel="noreferrer">
            Download
          </a>
        </div>
      </div>
    </header>
  );
}

function Hero() {
  const reduced = useReducedMotion();
  return (
    <section className="hero" id="top">
      {!reduced && (
        <div className="hero-bg" aria-hidden="true">
          <DotGrid dotSize={3} gap={28} baseColor="#20252c" activeColor="#3ddbb4" proximity={130} shockRadius={170} shockStrength={3} glow={0} />
        </div>
      )}
      <div className="hero-fade" aria-hidden="true" />
      <div className="wrap hero-inner">
        <p className="badge">
          <ShinyText text="Minecraft: Java Edition 26.3 · Written in C++" color="#9aa6b4" shineColor="#ecf9fb" speed={4} />
        </p>
        <h1>
          The game you know,
          <br />
          rebuilt in C++.
        </h1>
        <p className="lead">A native client with its own launcher: no Java, the game&rsquo;s own menus, and a world that is ready in seconds. It reads your copy of the game and is open source.</p>
        <div className="actions">
          <a className="btn primary big" href={RELEASE} target="_blank" rel="noreferrer">
            Download the launcher
          </a>
          <a className="btn big" href={REPO} target="_blank" rel="noreferrer">
            View on GitHub
          </a>
        </div>
        <p className="meta">
          Windows 10 or 11, and 64-bit Linux &middot; v{VERSION} &middot; needs a Microsoft account that owns Minecraft: Java Edition
        </p>
      </div>
    </section>
  );
}

function Benchmarks() {
  return (
    <section className="section" id="benchmarks">
      <div className="wrap">
        <Reveal>
          <h2>
            About <Num value={startupRatio} decimals={1} suffix="×" /> faster to start. About <Num value={memoryRatio} decimals={1} suffix="×" /> less memory.
          </h2>
          <p className="section-lead">
            Falcon and the vanilla Java client, 26.3, in the same world (same seed) at render distance 8, on one PC. These are start-up time and memory.{' '}
            <strong>Frame rate was not compared</strong>, and vanilla does more work than Falcon does; the notes below say how much that matters.
          </p>
        </Reveal>

        <div className="charts">
          <Reveal>
            <div className="chart">
              <h3>From starting the program to being in the world</h3>
              <p className="chart-sub">Seconds, lower is better. A new world that has to be generated.</p>
              <Bar label="Vanilla 26.3" value={startup.vanilla.seconds} decimals={1} suffix=" s" scale={startup.scale} note={`Two runs: ${startup.vanilla.runs}.`} />
              <Bar label="Falcon, first start" value={startup.falconFirst.seconds} decimals={1} suffix=" s" scale={startup.scale} falcon note={`Three runs: ${startup.falconFirst.runs}.`} />
              <Bar label="Falcon, later starts" value={startup.falconLater.seconds} decimals={1} suffix=" s" scale={startup.scale} falcon note={`Two runs: ${startup.falconLater.runs}.`} />
            </div>
          </Reveal>
          <Reveal delay={0.1}>
            <div className="chart">
              <h3>Memory 30 seconds after the world is ready</h3>
              <p className="chart-sub">Megabytes of working set (what Task Manager shows), lower is better.</p>
              <Bar label="Vanilla 26.3, render distance 8" value={memory.vanilla.mb} suffix=" MB" scale={memory.scale} note={`One run. Private memory ${memory.vanilla.privateMb} MB; run with the launcher’s default 2 GB heap limit.`} />
              <Bar label="Falcon, render distance 8" value={memory.falcon.mb} suffix=" MB" scale={memory.scale} falcon note={`Three runs. Private memory ${memory.falcon.privateMb} MB.`} />
              <Bar label="Falcon, render distance 16" value={memory.falcon16.mb} suffix=" MB" scale={memory.scale} falcon note={`One run. Private memory ${memory.falcon16.privateMb} MB. Vanilla was not measured at 16.`} />
            </div>
          </Reveal>
        </div>

        <Reveal>
          <div className="stats">
            <div className="stat">
              <div className="stat-num">
                <Num value={0.3} decimals={1} suffix=" s" />
              </div>
              <p>from starting the launcher to a page you can use</p>
            </div>
            <div className="stat">
              <div className="stat-num">
                <Num value={65} suffix=" MB" />
              </div>
              <p>for the whole launcher, including the system web view that draws it</p>
            </div>
            <div className="stat">
              <div className="stat-num">
                <Num value={33} prefix="−" suffix="%" />
              </div>
              <p>GPU time of the world pass at render distance 32, against Falcon&rsquo;s own earlier renderer (CPU time −36%, same pictures)</p>
            </div>
          </div>

          <details className="notes">
            <summary>How this was measured, and what it does not show</summary>
            <ul>
              {caveats.map((c) => (
                <li key={c.title}>
                  <strong>{c.title}</strong> {c.text}
                </li>
              ))}
            </ul>
            <p>
              The scripts, the raw numbers and the method are in the repository: <a href={`${REPO}/blob/main/docs/vs-vanilla.md`}>docs/vs-vanilla.md</a> and{' '}
              <a href={`${REPO}/blob/main/docs/performance.md`}>docs/performance.md</a>. Measured on 15 November 2026.
            </p>
          </details>
        </Reveal>
      </div>
    </section>
  );
}

function Features() {
  return (
    <section className="section" id="features">
      <div className="wrap">
        <Reveal>
          <h2>Same inputs, a different program.</h2>
          <p className="section-lead">Falcon reimplements the game&rsquo;s client, not the whole game. It reads your own copy of the game&rsquo;s files, so it looks like the game.</p>
        </Reveal>
        <div className="cards">
          {features.map((f, i) => (
            <Reveal key={f.title} delay={i * 0.06}>
              <SpotlightCard className="card" spotlightColor="rgba(61, 219, 180, 0.14)">
                <h3>{f.title}</h3>
                <p>{f.text}</p>
              </SpotlightCard>
            </Reveal>
          ))}
        </div>
      </div>
    </section>
  );
}

function Gallery() {
  return (
    <section className="section" id="gallery">
      <div className="wrap">
        <Reveal>
          <h2>What it looks like.</h2>
          <p className="section-lead">Screenshots of Falcon itself: the menus and the world come from the game&rsquo;s own files on the player&rsquo;s PC.</p>
        </Reveal>
        <div className="shots">
          {shots.map((s, i) => (
            <Reveal key={s.src} delay={(i % 2) * 0.08}>
              <figure>
                <img src={s.src} alt={s.alt} width="854" height="480" loading="lazy" />
                <figcaption>{s.caption}</figcaption>
              </figure>
            </Reveal>
          ))}
        </div>
      </div>
    </section>
  );
}

function How() {
  const steps = [
    { n: '1', title: 'Sign in', text: 'The launcher opens Microsoft’s own sign-in page. Your password never goes through Falcon. It then checks with Mojang that the account owns Java Edition.' },
    { n: '2', title: 'Download', text: 'On the first Play it downloads the game’s pictures, sounds and languages from Mojang for your account, and Falcon’s data tables from this project’s release.' },
    { n: '3', title: 'Play', text: 'It starts the client, which checks with Mojang again that the account owns the game before it opens.' },
  ];
  return (
    <section className="section" id="how">
      <div className="wrap">
        <Reveal>
          <h2>Your own copy.</h2>
          <p className="section-lead">Falcon contains no Minecraft textures, sounds or code. It uses the files of a copy of the game you own.</p>
        </Reveal>
        <div className="cards three">
          {steps.map((s, i) => (
            <Reveal key={s.n} delay={i * 0.06}>
              <SpotlightCard className="card" spotlightColor="rgba(61, 219, 180, 0.14)">
                <span className="step">{s.n}</span>
                <h3>{s.title}</h3>
                <p>{s.text}</p>
              </SpotlightCard>
            </Reveal>
          ))}
        </div>
      </div>
    </section>
  );
}

function Missing() {
  return (
    <section className="section" id="missing">
      <div className="wrap narrow">
        <Reveal>
          <h2>Not done yet.</h2>
          <p className="section-lead">Falcon is not finished. This is what is missing, so that nobody is surprised.</p>
          <ul className="plain-list">
            {missing.map((m) => (
              <li key={m}>{m}</li>
            ))}
          </ul>
          <p className="section-lead">
            The complete list is in the <a href={`${REPO}#readme`}>README</a>.
          </p>
        </Reveal>
      </div>
    </section>
  );
}

function Faq() {
  return (
    <section className="section" id="faq">
      <div className="wrap narrow">
        <Reveal>
          <h2>Questions.</h2>
          {faq.map((f) => (
            <details key={f.q} className="faq">
              <summary>{f.q}</summary>
              <p>{f.a}</p>
            </details>
          ))}
        </Reveal>
      </div>
    </section>
  );
}

function Footer() {
  return (
    <footer className="footer">
      <div className="wrap">
        <p>Falcon is an independent project. It is not affiliated with Mojang Studios or Microsoft. Minecraft is a trademark of Mojang Synergies AB.</p>
        <p>
          Open source under the Apache License 2.0 &middot; <a href={REPO}>Source</a> &middot; <a href={RELEASE}>Releases</a> &middot; Some interface components are from{' '}
          <a href="https://reactbits.dev">React Bits</a>. Benchmarks were measured on one PC on 15 November 2026.
        </p>
      </div>
    </footer>
  );
}

export default function App() {
  return (
    <>
      <Nav />
      <main>
        <Hero />
        <Benchmarks />
        <Features />
        <Gallery />
        <How />
        <Missing />
        <Faq />
      </main>
      <Footer />
    </>
  );
}
