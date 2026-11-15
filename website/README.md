# Falcon's site

A small Vite + React + TypeScript page, hosted on Vercel at https://falcon.literaluu.xyz.

```
npm install
npm run dev        # http://localhost:5173
npm run build      # type-checks, then writes dist/
vercel deploy --prod
```

- `src/data.ts`: every number and sentence the page states (the benchmark figures, the caveats, the FAQ). The figures are those of `docs/vs-vanilla.md`,
  `docs/benchmarks/vs-vanilla.json` and `docs/performance.md`; change them together, and say what was and was not measured. There is no frame-rate comparison with vanilla, and the page says so.
- `src/App.tsx`, `src/styles.css`: the page. The colours are the launcher's.
- `src/components/reactbits/`: [React Bits](https://reactbits.dev) components (CountUp, SpotlightCard, ShinyText, AnimatedContent, DotGrid), copied from its registry (`https://reactbits.dev/r/<Name>-TS-CSS.json`). They are used as they come.
  Motion is switched off for visitors who ask for reduced motion.
- `public/`: the Falcon icon and four screenshots of Falcon itself.
- `vercel.json`: build settings and headers. In the Vercel project the root directory is `website`.
