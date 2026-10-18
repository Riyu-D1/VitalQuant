import React from 'react';
import { Link } from 'react-router-dom';
import { ArrowUpRight, Cpu, Cloud, ArrowRightLeft } from 'lucide-react';
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

const TIERS = [
  {
    icon: Cpu,
    tag: 'Tier 1 · On-device',
    name: 'LAT-D-VQt-E1',
    role: 'Continuous monitor',
    text: 'A compact decision model running on the wearable itself — always on, always watching. Every epoch it reads the fused signal stack and returns a typed answer: stable, watch, or deteriorating, with a confidence score.',
    points: [
      'Runs entirely on the device — no link required',
      'Typed decisions, not text: a distribution over fixed clinical states',
      'Power-frugal by design — inference is a rounding error next to the sensors',
    ],
  },
  {
    icon: Cloud,
    tag: 'Tier 2 · Cloud',
    name: 'LAT-D-VQt-1',
    role: 'Verifier & escalation',
    text: 'The closed-weight clinical decision model, developed with specialist ML partners. It sees the cases LAT-D-VQt-E1 escalates — deeper evaluation over more context, with the confidence calibration needed to act on the answer.',
    points: [
      'Beats the incumbent early-warning score on every question it answers',
      'Calibrated confidence — honest enough to act on selectively',
      'Wearable-only inputs: nothing it needs that the device cannot measure',
    ],
  },
];

export default function Model() {
  return (
    <div className="bg-caladan-dark text-white">
      {/* Header */}
      <section className="relative pt-44 pb-20 text-center overflow-hidden">
        <div
          className="absolute inset-0 pointer-events-none"
          style={{
            background: `radial-gradient(ellipse 50% 45% at 50% 40%, rgba(${GREEN_RGB}, 0.10), transparent 70%)`,
          }}
        />
        <div className="relative max-w-4xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <Eyebrow>The decision layer</Eyebrow>
          </FadeContent>
          <h1 className="font-rx100 mt-6">
            <SplitText
              text="Two-tier intelligence."
              className="block text-5xl lg:text-7xl text-white"
              splitType="chars"
              delay={45}
              duration={0.9}
              textAlign="center"
            />
          </h1>
          <FadeContent delay={500} duration={1000}>
            <p className="mt-8 text-xl text-gray-300 leading-relaxed font-light">
              VitalQ doesn't send everything to the cloud. A small model on the
              device itself watches continuously — and escalates to a larger
              verifier only when it sees something worth a second opinion.
            </p>
          </FadeContent>
        </div>
      </section>

      {/* Two tiers */}
      <section className="py-20 border-t border-zinc-800/60">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
          <div className="grid gap-6 md:grid-cols-2">
            {TIERS.map((t, i) => (
              <FadeContent key={t.name} delay={i * 150}>
                <SpotlightCard
                  className="h-full p-8 border-zinc-800/80 bg-zinc-950/80 card-lift"
                  spotlightColor={`rgba(${GREEN_RGB}, 0.15)`}
                >
                  <div className="flex items-center justify-between">
                    <t.icon className="h-6 w-6 text-caladan-green" />
                    <span className="text-[11px] font-semibold uppercase tracking-[0.2em] text-zinc-500">
                      {t.tag}
                    </span>
                  </div>
                  <h2 className="mt-6 text-3xl font-rx100 text-white">{t.name}</h2>
                  <p className="mt-1 text-xs font-semibold uppercase tracking-[0.2em] text-caladan-green">
                    {t.role}
                  </p>
                  <p className="mt-5 text-sm text-gray-400 leading-relaxed">{t.text}</p>
                  <ul className="mt-6 space-y-3">
                    {t.points.map((p) => (
                      <li key={p} className="flex items-start gap-3 text-sm text-gray-300">
                        <span className="eyebrow-dot mt-1.5 shrink-0" />
                        {p}
                      </li>
                    ))}
                  </ul>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
        </div>
      </section>

      {/* The cascade */}
      <section className="py-20 border-t border-zinc-800/60 bg-zinc-950/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">02</span>
            <div className="mt-2">
              <Eyebrow />
            </div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">
              How the cascade works
            </h2>
          </FadeContent>
          <FadeContent delay={150}>
            <div className="mt-6 text-gray-300 leading-relaxed space-y-4 text-lg">
              <p>
                <span className="text-white font-medium">LAT-D-VQt-E1</span> screens every
                epoch locally. Most of the time the answer is "stable" and nothing
                leaves the device — no latency, no link dependency, no battery spent
                on radios.
              </p>
              <p>
                When the local model's confidence drops or its verdict turns
                suspicious, it escalates: the window is shipped to{' '}
                <span className="text-white font-medium">LAT-D-VQt-1</span> for a
                deeper evaluation. The care team sees the cloud verdict, not the raw
                stream — an alert that has already survived two tiers of scrutiny is
                an alert worth acting on.
              </p>
              <p className="text-caladan-green font-semibold text-xl">
                The result: continuous coverage with the battery and latency of an
                edge device, and the judgement of a much larger model exactly where
                it earns its keep.
              </p>
            </div>
          </FadeContent>
          <FadeContent delay={250}>
            <div className="mt-8 flex items-center gap-3 text-sm text-gray-500">
              <ArrowRightLeft className="h-4 w-4 text-caladan-green shrink-0" />
              <span>
                The escalation threshold is tuned for sensitivity — LAT-D-VQt-E1 passes the
                borderline up rather than guessing at the edge.
              </span>
            </div>
          </FadeContent>
        </div>
      </section>

      {/* CTA */}
      <section className="py-20 border-t border-zinc-800/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8 text-center">
          <FadeContent>
            <h2 className="font-rx100 text-3xl md:text-4xl text-white tracking-tight">
              Details under NDA
            </h2>
            <p className="mt-4 text-gray-400 text-lg">
              Model architecture, training protocol and evaluation results are
              shared selectively while we're in stealth. Talk to us.
            </p>
            <Link
              to="/contact"
              className="mt-8 inline-flex items-center gap-2 rounded-full bg-caladan-green px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-black transition-colors hover:bg-white"
            >
              Contact
              <ArrowUpRight size={16} />
            </Link>
          </FadeContent>
        </div>
      </section>
    </div>
  );
}
