import React from 'react';
import { Link } from 'react-router-dom';
import { ArrowRight, MapPin } from 'lucide-react';
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

const TEAM = [
  {
    initials: 'RD',
    name: 'Riyansh Diwan',
    role: 'Co-Founder & CEO',
    bio: 'Clinical problem, partnerships and fundraising. Leads strategy and the hardware roadmap.',
  },
  {
    initials: 'MM',
    name: 'Moses Man',
    role: 'Co-Founder & CTO',
    bio: 'Optics, firmware and the full-stack platform — sensor driver to decision model.',
  },
  {
    initials: 'AL',
    name: 'Andre Law',
    role: 'CFO',
    bio: 'Unit economics, runway and the numbers that keep the lights on.',
  },
];

export default function About() {
  return (
    <div className="bg-caladan-dark text-white">
      {/* Hero */}
      <section className="relative pt-44 pb-16 text-center overflow-hidden">
        <div
          className="absolute inset-0 pointer-events-none"
          style={{
            background: `radial-gradient(ellipse 50% 45% at 50% 40%, rgba(${GREEN_RGB}, 0.10), transparent 70%)`,
          }}
        />
        <div className="relative max-w-4xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <Eyebrow>About</Eyebrow>
          </FadeContent>
          <h1 className="font-rx100 mt-6">
            <SplitText
              text="A small team in London, building an early-warning wearable."
              className="block text-4xl lg:text-6xl text-white"
              splitType="words"
              delay={45}
              duration={0.9}
              textAlign="center"
            />
          </h1>
          <FadeContent delay={500} duration={1000}>
            <p className="mt-8 text-xl text-gray-300 leading-relaxed font-light">
              VitalQuant is an early-stage research and prototyping company. We build a
              multimodal wearable sensing platform — custom hardware, a provenance-first
              data pipeline, and a clinical decision model — aimed at catching
              post-operative deterioration earlier than standard monitoring.
            </p>
          </FadeContent>
        </div>
      </section>

      {/* Who / where / stage */}
      <section className="py-16 border-t border-zinc-800/60 bg-zinc-950/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">01</span>
            <div className="mt-2">
              <Eyebrow />
            </div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">
              Where we are, honestly
            </h2>
          </FadeContent>
          <FadeContent delay={150}>
            <div className="mt-6 text-gray-300 leading-relaxed space-y-4 text-lg">
              <p>
                <span className="inline-flex items-center gap-2 text-white font-medium">
                  <MapPin className="h-4 w-4 text-caladan-green" /> London, United Kingdom.
                </span>{' '}
                Three people, one lab bench, and a repo we work in every day.
              </p>
              <p>
                <span className="text-white font-medium">Stage:</span> early research plus a
                working prototype. The sensing platform runs end-to-end today — breakout
                hardware, ingest API, processing pipeline, synthetic-device test harness —
                and our first clinical decision model, LAT-D-VQt-1, exists as a
                closed-weight model trained and evaluated by LatticeAG.
              </p>
              <p>
                We are <span className="text-white font-medium">not a medical device
                yet</span> — nothing here has been through clinical validation or regulatory
                review, and model outputs are experimental scores, not diagnoses. Clinical
                deployment is the goal; research is where we are.
              </p>
            </div>
          </FadeContent>
        </div>
      </section>

      {/* Team */}
      <section className="py-16 border-t border-zinc-800/60">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">02</span>
            <div className="mt-2">
              <Eyebrow />
            </div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">
              The team
            </h2>
          </FadeContent>
          <div className="mt-10 grid grid-cols-1 md:grid-cols-3 gap-5">
            {TEAM.map((m, i) => (
              <FadeContent key={m.name} delay={i * 120} duration={900}>
                <SpotlightCard
                  className="h-full border-zinc-800/80 bg-zinc-950/80 card-lift"
                  spotlightColor={`rgba(${GREEN_RGB}, 0.12)`}
                >
                  <div className="w-14 h-14 rounded-full border border-caladan-green/40 bg-caladan-green/10 flex items-center justify-center text-caladan-green font-rx100 text-lg">
                    {m.initials}
                  </div>
                  <h3 className="mt-6 text-xl font-rx100 text-white">{m.name}</h3>
                  <div className="mt-1 text-xs font-semibold uppercase tracking-[0.2em] text-caladan-green">
                    {m.role}
                  </div>
                  <p className="mt-4 text-sm text-gray-400 leading-relaxed">{m.bio}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
        </div>
      </section>

      {/* CTA */}
      <section className="py-16 border-t border-zinc-800/60 bg-zinc-950/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8 text-center">
          <FadeContent>
            <p className="text-gray-400 text-lg">
              Everything we claim is in the repo and the paper — hardware profiles, the
              ingest protocol, the pipeline, the model layer.
            </p>
            <div className="mt-8 flex flex-wrap items-center justify-center gap-4">
              <Link
                to="/paper"
                className="inline-flex items-center gap-2 rounded-full bg-caladan-green px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-black transition-colors hover:bg-white"
              >
                Read the paper
              </Link>
              <Link
                to="/contact"
                className="group inline-flex items-center gap-2 rounded-full border border-zinc-700 px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-gray-300 transition-colors hover:border-caladan-green hover:text-caladan-green"
              >
                Get in touch
                <ArrowRight className="w-4 h-4 transition-transform duration-300 group-hover:translate-x-1" />
              </Link>
            </div>
          </FadeContent>
        </div>
      </section>
    </div>
  );
}
