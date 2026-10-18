import React from 'react';
import { Link } from 'react-router-dom';
import {
  Activity,
  ArrowUpRight,
  BrainCircuit,
  Clock,
  Radio,
  ShieldCheck,
  Thermometer,
  Waves,
  Hand,
} from 'lucide-react';
import SplitText from '../components/ui/SplitText';
import FadeContent from '../components/ui/FadeContent';
import SpotlightCard from '../components/ui/SpotlightCard';

const GREEN_RGB = '20, 200, 113';

function Eyebrow({ children }) {
  return (
    <span className="eyebrow">
      <span className="eyebrow-dot" />
      {children}
    </span>
  );
}

const SENSES = [
  {
    icon: Activity,
    name: 'Perfusion & oxygenation',
    detail:
      'Continuous optical readout of microvascular dynamics — the channels that move first when circulation begins to fail.',
  },
  {
    icon: Thermometer,
    name: 'Temperature trends',
    detail:
      'Skin-surface temperature tracked continuously — trends, not spot readings.',
  },
  {
    icon: Waves,
    name: 'Ambient context',
    detail:
      'The environment around the patient is measured too, so the system can tell a change in the room from a change in the patient.',
  },
  {
    icon: Radio,
    name: 'Motion context',
    detail:
      'Movement is a first-class channel: it gates signal quality so artefact never silently becomes data.',
  },
  {
    icon: Hand,
    name: 'Wear detection',
    detail:
      'The device knows when it is actually on skin — an honest "no signal" beats a bad one.',
  },
  {
    icon: ShieldCheck,
    name: 'Signal honesty',
    detail:
      'Every reading carries a quality index. Bad windows are abstained from — never imputed, never scored.',
  },
];

const PLATFORM = [
  {
    title: 'Always on',
    body: 'A chest-worn device streams continuously while worn — no spot checks, no gaps between nurse rounds.',
  },
  {
    title: 'Quality-gated',
    body: 'Signal quality is scored before anything reaches a model. Motion noise is filtered at the edge, not averaged into the answer.',
  },
  {
    title: 'Patient-specific',
    body: 'Rolling per-patient baselines flag deviation from normal for that patient — not a population average.',
  },
  {
    title: 'Workflow-ready',
    body: 'Alerts are designed to reach the care team through existing channels — actionable, ranked, and honest about confidence.',
  },
];

const RESULTS = [
  {
    value: '6–12 h',
    label: 'earlier detection',
    text: 'The target window: surfacing deterioration hours before the clinical picture is obvious.',
  },
  {
    value: 'Wearable-only',
    label: 'input scope',
    text: 'Every input is something the device measures continuously — no invasive-only signals, ever.',
  },
  {
    value: 'Calibrated',
    label: 'confidence',
    text: 'Answers ship with confidence honest enough to act on selectively — and to abstain when the signal is not there.',
  },
];

export default function Product() {
  return (
    <div className="bg-caladan-dark text-white">
      {/* Hero */}
      <section className="relative pt-44 pb-20 text-center overflow-hidden">
        <div
          className="absolute inset-0 pointer-events-none"
          style={{
            background: `radial-gradient(ellipse 50% 45% at 50% 40%, rgba(${GREEN_RGB}, 0.10), transparent 70%)`,
          }}
        />
        <div className="relative max-w-4xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <Eyebrow>Product</Eyebrow>
          </FadeContent>
          <h1 className="font-rx100 mt-6">
            <SplitText
              text="Skin to signal. Signal to answer."
              className="block text-5xl lg:text-6xl text-white"
              splitType="words"
              delay={45}
              duration={0.9}
              textAlign="center"
            />
          </h1>
          <FadeContent delay={500} duration={1000}>
            <p className="mt-8 text-xl text-gray-300 leading-relaxed font-light">
              VitalQ is a wearable early-warning platform: continuous optical sensing,
              a quality-gated signal pipeline, and a clinical decision model — built to
              catch physiological deterioration hours before conventional monitoring.
            </p>
          </FadeContent>
          <FadeContent delay={800} duration={1000}>
            <div className="mt-10 flex flex-wrap items-center justify-center gap-4">
              <Link
                to="/tryit"
                className="inline-flex items-center gap-2 rounded-full bg-caladan-green px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-black transition-colors hover:bg-white"
              >
                Try the demo
                <ArrowUpRight size={16} />
              </Link>
              <Link
                to="/contact"
                className="inline-flex items-center gap-2 rounded-full border border-zinc-700 px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-gray-300 transition-colors hover:border-caladan-green hover:text-caladan-green"
              >
                Contact
              </Link>
            </div>
          </FadeContent>
        </div>
      </section>

      {/* What it senses */}
      <section className="py-20 border-t border-zinc-800/60 bg-zinc-950/60">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">01</span>
            <div className="mt-2">
              <Eyebrow />
            </div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">
              What it senses
            </h2>
            <p className="mt-6 text-gray-300 leading-relaxed text-lg max-w-3xl">
              A multi-modal array fused in a single wearable — every channel chosen
              because it moves early in deterioration. The specifics stay under wraps
              while we're in stealth.
            </p>
          </FadeContent>
          <div className="mt-10 grid gap-6 md:grid-cols-2">
            {SENSES.map((s, i) => (
              <FadeContent key={s.name} delay={i * 100}>
                <SpotlightCard className="h-full p-6" spotlightColor={`rgba(${GREEN_RGB}, 0.15)`}>
                  <div className="flex items-center gap-3">
                    <s.icon className="h-5 w-5 text-caladan-green" />
                    <p className="font-rx100 text-lg text-white">{s.name}</p>
                  </div>
                  <p className="mt-3 text-sm text-gray-400 leading-relaxed">{s.detail}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
        </div>
      </section>

      {/* The platform idea */}
      <section className="py-20 border-t border-zinc-800/60">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">02</span>
            <div className="mt-2">
              <Eyebrow />
            </div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">
              The platform
            </h2>
            <p className="mt-6 text-gray-300 leading-relaxed text-lg max-w-3xl">
              From the device to the care team: secure telemetry, a quality-gated
              processing pipeline, and a scoring layer that refuses to lose the
              difference between a real signal and a noisy one.
            </p>
          </FadeContent>
          <div className="mt-10 grid gap-6 md:grid-cols-2">
            {PLATFORM.map((s, i) => (
              <FadeContent key={s.title} delay={i * 100}>
                <SpotlightCard className="h-full p-6" spotlightColor={`rgba(${GREEN_RGB}, 0.15)`}>
                  <p className="font-rx100 text-lg text-white">{s.title}</p>
                  <p className="mt-3 text-sm text-gray-400 leading-relaxed">{s.body}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
        </div>
      </section>

      {/* The model */}
      <section className="py-20 border-t border-zinc-800/60 bg-zinc-950/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">03</span>
            <div className="mt-2">
              <Eyebrow />
            </div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">
              The decision layer
            </h2>
          </FadeContent>
          <FadeContent delay={150}>
            <div className="mt-6 text-gray-300 leading-relaxed space-y-4 text-lg">
              <p>
                On top of the pipeline sits a closed-weight clinical decision model,
                developed with specialist ML partners. It answers a fixed set of
                deterioration questions from wearable channels only — a model that
                quietly depends on a channel the hardware cannot supply is useless in
                the setting it was designed for.
              </p>
            </div>
          </FadeContent>
          <div className="mt-10 grid gap-6 md:grid-cols-3">
            {RESULTS.map((f, i) => (
              <FadeContent key={f.label} delay={i * 120}>
                <SpotlightCard className="h-full p-5" spotlightColor={`rgba(${GREEN_RGB}, 0.18)`}>
                  <p className="font-rx100 text-2xl text-white leading-tight">{f.value}</p>
                  <p className="mt-1 text-xs uppercase tracking-[0.2em] text-caladan-green">
                    {f.label}
                  </p>
                  <p className="mt-4 text-sm text-gray-400 leading-relaxed">{f.text}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
          <FadeContent delay={200}>
            <div className="mt-8 flex flex-wrap gap-4">
              <Link
                to="/model"
                className="inline-flex items-center gap-2 rounded-full bg-caladan-green px-6 py-3 text-sm font-semibold text-caladan-dark transition hover:brightness-110"
              >
                <BrainCircuit className="h-4 w-4" /> Model page
              </Link>
            </div>
          </FadeContent>
        </div>
      </section>

      {/* CTA */}
      <section className="py-20 border-t border-zinc-800/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8 text-center">
          <FadeContent>
            <Clock className="h-8 w-8 text-caladan-green mx-auto" />
            <h2 className="mt-4 font-rx100 text-3xl md:text-4xl text-white tracking-tight">
              Hours earlier is the whole point
            </h2>
            <p className="mt-4 text-gray-400 text-lg">
              Every hour of earlier detection moves the outcome. We're in stealth —
              the technical detail is shared selectively, under NDA.
            </p>
            <div className="mt-8 flex flex-wrap items-center justify-center gap-4">
              <Link
                to="/tryit"
                className="inline-flex items-center gap-2 rounded-full bg-caladan-green px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-black transition-colors hover:bg-white"
              >
                See it in action
              </Link>
              <Link
                to="/contact"
                className="inline-flex items-center gap-2 rounded-full border border-zinc-700 px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-gray-300 transition-colors hover:border-caladan-green hover:text-caladan-green"
              >
                Contact us
              </Link>
            </div>
          </FadeContent>
        </div>
      </section>
    </div>
  );
}
