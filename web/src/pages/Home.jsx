import React from 'react';
import { Link } from 'react-router-dom';
import { ArrowRight, Activity, Waves, Cpu, ScanLine, ChevronDown } from 'lucide-react';
import Dither from '../components/ui/Dither';
import SplitText from '../components/ui/SplitText';
import FadeContent from '../components/ui/FadeContent';
import CountUp from '../components/ui/CountUp';
import SpotlightCard from '../components/ui/SpotlightCard';
import ScrollVelocity from '../components/ui/ScrollVelocity';
import ShinyText from '../components/ui/ShinyText';

const GREEN_RGB = '20, 200, 113';

const STATS = [
  { value: 310, suffix: 'M+', label: 'Surgeries annually', sub: 'Every one of them at risk of post-operative infection.' },
  { value: 70, suffix: '%', label: 'Survival drop', sub: 'when sepsis is caught late instead of early.' },
  { value: 12, suffix: 'h', label: 'Current detection lag', sub: 'between first physiological changes and diagnosis.' },
];

const STEPS = [
  {
    num: '01',
    icon: ScanLine,
    title: 'Sense',
    text: 'A wearable optical array — multi-wavelength PPG, spectral imaging, thermal — captures tissue perfusion and microvascular dynamics in real time.',
  },
  {
    num: '02',
    icon: Waves,
    title: 'Stream',
    text: 'Encrypted telemetry flows through a signal-quality-gated pipeline. Motion noise is filtered before anything touches a score.',
  },
  {
    num: '03',
    icon: Cpu,
    title: 'Score',
    text: 'Patient-specific baselines and pattern recognition surface deterioration hours before clinical signs — routed to the care team.',
  },
];

const FEATURES = [
  {
    num: '01',
    tag: 'The wearable — exists today',
    title: 'Six-Modal Sensor Node',
    text: 'Our hw_v0 rig is built and streaming: PPG, spectral, thermal, environment, motion and contact sensing on one ESP32, feeding real signal into the pipeline. The custom PCB (hw_v1) is the next revision.',
    img: '/image.png',
    alt: 'VitalQuant sensor hardware',
    bullets: [
      'Dual-wavelength PPG + 11-channel spectral',
      'Thermal, environment, motion, contact',
      'On-device buffering with clock discipline',
    ],
  },
  {
    num: '02',
    tag: 'The platform — exists today',
    title: 'Quality-Gated Pipeline + Decision Model',
    text: 'In the repo now: an idempotent ingest API, per-session clock correction, signal-quality gates, a baseline anomaly layer — and LAT-D-VQt-1, our closed-weight clinical decision model built with LatticeAG.',
    img: '/image copy.png',
    alt: 'VitalQuant data platform',
    bullets: [
      'SQI gates on every window — bad signal abstained, not imputed',
      'Per-subject baselines, label-free anomaly scoring',
      'LAT-D-VQt-1 answers a fixed deterioration question set',
    ],
    reverse: true,
  },
  {
    num: '03',
    tag: 'Clinical deployment — next',
    title: 'Built for Post-Op Care Teams',
    text: 'The target user is the clinician watching post-operative patients: deterioration alerts that reach the ward, not another dashboard. Workflow integration is planned — today this is a research platform, not a deployed device.',
    img: '/image copy 2.png',
    alt: 'Clinical alerting for post-operative care',
    bullets: [
      'Built for clinicians and post-op care pathways',
      'Alert routing and EMR integration on the roadmap',
      'Research-stage today — validation before deployment',
    ],
  },
];

const TEAM = [
  {
    initials: 'RD',
    name: 'Riyansh Diwan',
    role: 'Co-Founder & CEO',
    bio: 'Vision, partnerships, and the clinical problem. Leads strategy and fundraising. Head of engineering, research, and hardware.',
  },
  {
    initials: 'MM',
    name: 'Moses Man',
    role: 'Co-Founder & CTO',
    bio: 'Optics, firmware, and the full-stack platform. Research lead. Leads software architecture.',
  },
  {
    initials: 'AL',
    name: 'Andre Law',
    role: 'CFO',
    bio: 'Unit economics, runway, and the numbers that keep the lights on.',
  },
];

function Eyebrow({ children }) {
  return (
    <span className="eyebrow">
      <span className="eyebrow-dot" />
      {children}
    </span>
  );
}

export default function Home() {
  return (
    <div className="bg-caladan-dark text-white">

      {/* ───────── HERO ───────── */}
      <section className="relative min-h-screen flex items-center justify-center overflow-hidden">
        <div className="absolute inset-0 z-0">
          <Dither
            waveColor={[0.039, 0.619, 0.349]}
            enableMouseInteraction={true}
            mouseRadius={0.3}
            colorNum={4}
            pixelSize={2}
            waveAmplitude={0.3}
            waveFrequency={3}
            waveSpeed={0.05}
          />
        </div>
        <div className="absolute inset-0 bg-gradient-to-t from-caladan-dark via-transparent to-black/40 z-[1] pointer-events-none" />

        <div className="relative z-10 max-w-6xl mx-auto px-6 pt-28 pb-20 text-center">
          <FadeContent delay={200} duration={900}>
            <div className="inline-flex items-center gap-3 border border-caladan-green/30 bg-black/40 backdrop-blur-sm rounded-full px-4 py-1.5 mb-8">
              <span className="relative flex h-2 w-2">
                <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-caladan-green opacity-60" />
                <span className="relative inline-flex rounded-full h-2 w-2 bg-caladan-green" />
              </span>
              <ShinyText
                text="Quantum-Enhanced Biosensing"
                className="text-[11px] font-semibold uppercase tracking-[0.25em]"
                color="#9ca3af"
                shineColor="#14C871"
                speed={3}
              />
            </div>
          </FadeContent>

          <h1 className="font-rx100 leading-[0.95] tracking-tight">
            <SplitText
              text="Detecting sepsis before"
              className="block text-5xl md:text-7xl lg:text-8xl text-white"
              splitType="chars"
              delay={35}
              duration={0.9}
              ease="power3.out"
              textAlign="center"
            />
            <SplitText
              text="symptoms appear."
              className="block text-5xl md:text-7xl lg:text-8xl text-caladan-green text-glow-green"
              splitType="chars"
              delay={35}
              duration={0.9}
              ease="power3.out"
              textAlign="center"
            />
          </h1>

          <FadeContent delay={900} duration={1000}>
            <p className="mt-8 text-lg md:text-xl text-gray-300 max-w-2xl mx-auto leading-relaxed">
              VitalQuant is a wearable sensing platform for clinicians and post-op care
              teams — continuous optical telemetry through a quality-gated pipeline into a
              clinical decision model, to surface deterioration before symptoms appear.
            </p>
          </FadeContent>

          <FadeContent delay={1200} duration={1000}>
            <div className="mt-10 flex flex-col sm:flex-row items-center justify-center gap-4">
              <Link
                to="/product"
                className="group inline-flex items-center gap-2 bg-caladan-green text-black font-semibold px-8 py-4 rounded-full text-sm tracking-wide hover:bg-white transition-colors duration-300"
              >
                Explore the platform
                <ArrowRight className="w-4 h-4 transition-transform duration-300 group-hover:translate-x-1" />
              </Link>
              <Link
                to="/contact"
                className="inline-flex items-center gap-2 border border-white/20 text-white font-medium px-8 py-4 rounded-full text-sm tracking-wide hover:border-caladan-green/60 hover:text-caladan-green transition-colors duration-300"
              >
                Schedule a meeting
              </Link>
            </div>
          </FadeContent>
        </div>

        <div className="absolute bottom-8 left-1/2 -translate-x-1/2 z-10 float-y">
          <ChevronDown className="w-5 h-5 text-caladan-green/70" />
        </div>
      </section>

      {/* ───────── TECH MARQUEE ───────── */}
      <div className="border-y border-zinc-800/60 bg-black/40 py-5">
        <ScrollVelocity
          texts={[
            'Squeezed-Light Photonics · Multi-Wavelength Spectroscopy · Continuous PPG · Signal-Quality Index · Patient-Specific Baselines · Real-Time Alerting · ',
          ]}
          velocity={60}
          className="text-sm uppercase tracking-[0.3em] text-zinc-500"
        />
      </div>

      {/* ───────── STATS ───────── */}
      <section className="py-24 md:py-32">
        <div className="max-w-6xl mx-auto px-6">
          <FadeContent>
            <Eyebrow>The gap in care</Eyebrow>
            <h2 className="mt-4 text-3xl md:text-5xl font-rx100 text-white leading-tight max-w-2xl">
              Hours of warning exist. They go unseen.
            </h2>
          </FadeContent>

          <div className="mt-14 grid md:grid-cols-3 gap-5">
            {STATS.map((s, i) => (
              <FadeContent key={s.label} delay={i * 120} duration={900}>
                <SpotlightCard
                  className="h-full border-zinc-800/80 bg-zinc-950/80 card-lift"
                  spotlightColor={`rgba(${GREEN_RGB}, 0.14)`}
                >
                  <div className="text-5xl md:text-6xl font-rx100 text-caladan-green">
                    <CountUp to={s.value} duration={1.8} separator="," />
                    <span>{s.suffix}</span>
                  </div>
                  <div className="mt-3 text-xs font-semibold uppercase tracking-[0.2em] text-white">
                    {s.label}
                  </div>
                  <p className="mt-3 text-sm text-gray-400 leading-relaxed">{s.sub}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
        </div>
      </section>

      <div className="hairline mx-6" />

      {/* ───────── HOW IT WORKS ───────── */}
      <section className="py-24 md:py-32">
        <div className="max-w-6xl mx-auto px-6">
          <FadeContent>
            <Eyebrow>Pipeline</Eyebrow>
            <h2 className="mt-4 text-3xl md:text-5xl font-rx100 text-white leading-tight max-w-2xl">
              From photons to early warning.
            </h2>
          </FadeContent>

          <div className="mt-14 grid md:grid-cols-3 gap-5 relative">
            <div className="hidden md:block absolute top-16 left-[18%] right-[18%] hairline" />
            {STEPS.map((step, i) => (
              <FadeContent key={step.num} delay={i * 140} duration={900}>
                <SpotlightCard
                  className="h-full border-zinc-800/80 bg-zinc-950/80 card-lift"
                  spotlightColor={`rgba(${GREEN_RGB}, 0.12)`}
                >
                  <div className="flex items-center justify-between">
                    <step.icon className="w-6 h-6 text-caladan-green" />
                    <span className="text-xs font-rx100 text-zinc-600">{step.num}</span>
                  </div>
                  <h3 className="mt-6 text-xl font-rx100 text-white">{step.title}</h3>
                  <p className="mt-3 text-sm text-gray-400 leading-relaxed">{step.text}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
        </div>
      </section>

      {/* ───────── PRODUCT FEATURES ───────── */}
      <section id="product" className="py-24 md:py-32 border-t border-zinc-800/60">
        <div className="max-w-6xl mx-auto px-6">
          <FadeContent>
            <Eyebrow>What it is, and who it's for</Eyebrow>
            <h2 className="mt-4 text-3xl md:text-5xl font-rx100 text-white leading-tight max-w-2xl">
              What exists today — and what comes next.
            </h2>
            <p className="mt-5 text-gray-400 max-w-2xl leading-relaxed">
              For clinicians and post-op care teams. The sensor and the platform are real
              and running; clinical deployment is the roadmap, not a claim.
            </p>
          </FadeContent>

          <div className="mt-20 space-y-24 md:space-y-32">
            {FEATURES.map((f, i) => (
              <div
                key={f.num}
                className={`grid md:grid-cols-2 gap-10 md:gap-16 items-center ${
                  f.reverse ? 'md:[&>*:first-child]:order-2' : ''
                }`}
              >
                <FadeContent duration={1000}>
                  <div>
                    <span className="text-xs font-rx100 text-zinc-600">{f.num}</span>
                    <div className="mt-2 text-[11px] font-semibold uppercase tracking-[0.22em] text-caladan-green">
                      {f.tag}
                    </div>
                    <h3 className="mt-4 text-2xl md:text-4xl font-rx100 text-white leading-tight">
                      {f.title}
                    </h3>
                    <p className="mt-5 text-gray-400 leading-relaxed">{f.text}</p>
                    <ul className="mt-6 space-y-3">
                      {f.bullets.map((b) => (
                        <li key={b} className="flex items-start gap-3 text-sm text-gray-300">
                          <Activity className="w-4 h-4 text-caladan-green mt-0.5 shrink-0" />
                          {b}
                        </li>
                      ))}
                    </ul>
                  </div>
                </FadeContent>
                <FadeContent duration={1000} delay={150}>
                  <div className="group relative rounded-3xl border border-zinc-800/80 bg-zinc-950 overflow-hidden card-lift">
                    <img
                      src={f.img}
                      alt={f.alt}
                      className="w-full h-72 md:h-96 object-cover img-zoom"
                    />
                    <div className="absolute inset-0 bg-gradient-to-t from-caladan-dark/60 to-transparent pointer-events-none" />
                  </div>
                </FadeContent>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* ───────── TEAM ───────── */}
      <section id="team" className="py-24 md:py-32 border-t border-zinc-800/60">
        <div className="max-w-6xl mx-auto px-6">
          <FadeContent>
            <Eyebrow>Team</Eyebrow>
            <h2 className="mt-4 text-3xl md:text-5xl font-rx100 text-white leading-tight">
              Built in London.
            </h2>
          </FadeContent>

          <div className="mt-14 grid md:grid-cols-3 gap-5">
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

      {/* ───────── CTA ───────── */}
      <section className="relative py-28 md:py-40 border-t border-zinc-800/60 overflow-hidden">
        <div
          className="absolute inset-0 pointer-events-none"
          style={{
            background: `radial-gradient(ellipse 60% 50% at 50% 60%, rgba(${GREEN_RGB}, 0.12), transparent 70%)`,
          }}
        />
        <div className="relative max-w-4xl mx-auto px-6 text-center">
          <SplitText
            text="Earlier detection. Better outcomes."
            className="text-4xl md:text-6xl font-rx100 text-white"
            splitType="words"
            delay={60}
            duration={0.9}
            textAlign="center"
          />
          <FadeContent delay={500} duration={900}>
            <p className="mt-6 text-gray-400 max-w-xl mx-auto">
              We're building the first quantum-enhanced wearable for clinical
              deterioration. Talk to us.
            </p>
            <div className="mt-10 flex flex-col sm:flex-row items-center justify-center gap-4">
              <Link
                to="/contact"
                className="group inline-flex items-center gap-2 bg-caladan-green text-black font-semibold px-8 py-4 rounded-full text-sm tracking-wide hover:bg-white transition-colors duration-300"
              >
                Get in touch
                <ArrowRight className="w-4 h-4 transition-transform duration-300 group-hover:translate-x-1" />
              </Link>
              <Link
                to="/tryit"
                className="inline-flex items-center gap-2 border border-white/20 text-white font-medium px-8 py-4 rounded-full text-sm tracking-wide hover:border-caladan-green/60 hover:text-caladan-green transition-colors duration-300"
              >
                See it in action
              </Link>
            </div>
          </FadeContent>
        </div>
      </section>
    </div>
  );
}
