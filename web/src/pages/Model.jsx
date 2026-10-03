import React from 'react';
import { Link } from 'react-router-dom';
import { ArrowUpRight } from 'lucide-react';
import SplitText from '../components/ui/SplitText';
import FadeContent from '../components/ui/FadeContent';
import SpotlightCard from '../components/ui/SpotlightCard';

const GREEN_RGB = '20, 200, 113';

// Full technical writeup lives on the LatticeAG side.
const LATTICEAG_MODEL_URL = 'https://latticeag.vercel.app/#/products/lat-d-vqt-1';

function Eyebrow({ children }) {
  return (
    <span className="eyebrow">
      <span className="eyebrow-dot" />
      {children}
    </span>
  );
}

function Section({ num, title, children, tint = false }) {
  return (
    <section className={`py-20 border-t border-zinc-800/60 ${tint ? 'bg-zinc-950/60' : ''}`}>
      <div className="max-w-4xl mx-auto px-6 lg:px-8">
        <FadeContent>
          <span className="text-xs font-rx100 text-zinc-600">{num}</span>
          <div className="mt-2">
            <Eyebrow />
          </div>
          <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">{title}</h2>
        </FadeContent>
        <FadeContent delay={150}>
          <div className="mt-6 text-gray-300 leading-relaxed space-y-4 text-lg">{children}</div>
        </FadeContent>
      </div>
    </section>
  );
}

const FACTS = [
  {
    value: '4/4',
    label: 'questions',
    text: 'It beats the incumbent early-warning score on every question it answers.',
  },
  {
    value: 'Wearable-only',
    label: 'input scope',
    text: 'Built from channels our sensor can supply. No invasive-only signals, ever.',
  },
  {
    value: 'Calibrated',
    label: 'confidence',
    text: 'Confidence error in the low single digits, so a score can be trusted selectively.',
  },
];

const LIMITS = [
  'Not a medical device. Outputs are experimental decision scores, not a diagnosis or a treatment recommendation.',
  'Not clinically validated. Nothing here has been through regulatory review.',
  'Not a chatbot. It answers a fixed clinical question set, not arbitrary requests.',
  'Not built on channels a wearable cannot supply - invasive-only signals are excluded by design.',
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
            <Eyebrow>Model</Eyebrow>
          </FadeContent>
          <h1 className="font-rx100 mt-6">
            <SplitText
              text="LAT-D-VQt-1"
              className="block text-5xl lg:text-7xl text-white"
              splitType="chars"
              delay={45}
              duration={0.9}
              textAlign="center"
            />
          </h1>
          <FadeContent delay={500} duration={1000}>
            <p className="mt-8 text-xl text-gray-300 leading-relaxed font-light">
              A closed-weight clinical decision model, built for VitalQ by LatticeAG.
              Post-trained on real clinical time-series to answer a fixed set of
              deterioration questions from the signals a wearable can actually measure.
            </p>
          </FadeContent>
          <FadeContent delay={800} duration={1000}>
            <div className="mt-10 flex flex-wrap items-center justify-center gap-4">
              <a
                href={LATTICEAG_MODEL_URL}
                target="_blank"
                rel="noreferrer"
                className="inline-flex items-center gap-2 rounded-full bg-caladan-green px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-black transition-colors hover:bg-white"
              >
                Technical writeup
                <ArrowUpRight size={16} />
              </a>
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

      <Section num="01" title="What it does" tint>
        <p>
          LAT-D-VQt-1 answers a fixed set of clinical questions about a patient's
          current state - whether they are deteriorating, and how far ahead that
          deterioration can be seen. It is not asked to write anything. Each answer
          comes with a confidence value, and those values are honest enough to act on
          selectively rather than read as a raw number.
        </p>
        <p>
          The model is built from the channels our wearable can supply. Every input it
          sees is something the device can measure continuously at the bedside, which
          is the whole point: a model that quietly depends on an invasive-only signal
          is useless in the setting it was designed for.
        </p>
        <p className="text-white font-medium">
          The weights are closed and held by VitalQ. LatticeAG ran the training and
          evaluation; the full technical record - protocol, ablation and results - is
          published on their side.
        </p>
      </Section>

      <section className="py-20 border-t border-zinc-800/60">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">02</span>
            <div className="mt-2">
              <Eyebrow />
            </div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">
              At a glance
            </h2>
          </FadeContent>
          <div className="mt-10 grid gap-6 md:grid-cols-3">
            {FACTS.map((f, i) => (
              <FadeContent key={f.label} delay={i * 120}>
                <SpotlightCard
                  className="h-full"
                  spotlightColor={`rgba(${GREEN_RGB}, 0.18)`}
                >
                  <p className="font-rx100 text-2xl text-white leading-tight">{f.value}</p>
                  <p className="mt-1 text-xs uppercase tracking-[0.2em] text-caladan-green">
                    {f.label}
                  </p>
                  <p className="mt-4 text-sm text-gray-400 leading-relaxed">{f.text}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
        </div>
      </section>

      <Section num="03" title="What it is not">
        <ul className="space-y-3">
          {LIMITS.map((l) => (
            <li key={l} className="flex gap-3">
              <span className="mt-2 h-1.5 w-1.5 flex-none rounded-full bg-caladan-green/70" />
              <span>{l}</span>
            </li>
          ))}
        </ul>
        <p className="text-gray-400">
          The technical page on LatticeAG carries the evaluation detail, including the
          slices that test whether the model depends on channels our sensor may not
          provide.
        </p>
      </Section>

      {/* CTA */}
      <section className="py-20 border-t border-zinc-800/60 bg-zinc-950/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8 text-center">
          <FadeContent>
            <h2 className="font-rx100 text-3xl md:text-4xl text-white tracking-tight">
              Read the technical writeup
            </h2>
            <p className="mt-4 text-gray-400 text-lg">
              Architecture, evaluation protocol, ablations and the data-scale study are
              documented on the LatticeAG product page.
            </p>
            <a
              href={LATTICEAG_MODEL_URL}
              target="_blank"
              rel="noreferrer"
              className="mt-8 inline-flex items-center gap-2 rounded-full bg-caladan-green px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-black transition-colors hover:bg-white"
            >
              LAT-D-VQt-1 on LatticeAG
              <ArrowUpRight size={16} />
            </a>
          </FadeContent>
        </div>
      </section>
    </div>
  );
}