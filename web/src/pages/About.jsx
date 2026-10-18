import React from 'react';
import { Link } from 'react-router-dom';
import { ArrowRight } from 'lucide-react';
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

function Chapter({ num, title, children, tint = false }) {
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

const WHY_NOW = [
  {
    title: 'Sensing hardware breakthrough',
    text: 'A measurement capability that once required benchtop lab equipment can now be integrated into a wearable form factor — a breakthrough of the last few years.',
  },
  {
    title: 'Advanced AI and machine learning',
    text: 'Modern algorithms can detect subtle patterns in multivariate biomarker data that would be invisible to human observation.',
  },
  {
    title: 'Clinical data availability',
    text: 'Large-scale patient datasets enable us to train and validate our models with unprecedented accuracy.',
  },
];

const TEAM = [
  {
    initials: 'RD',
    name: 'Riyansh Diwan',
    role: 'Co-Founder & CEO',
    bio: 'Visionary leader bridging the gap between advanced sensors and practical clinical deployments.',
  },
  {
    initials: 'MM',
    name: 'Moses Man',
    role: 'Co-Founder & CTO',
    bio: "Leading VitalQ's technology strategy and development of its proprietary sensing platform.",
  },
];

const TIMELINE = [
  { year: '2026', text: 'VitalQ founded with mission to bring next-generation sensing to clinical care', active: true },
  { year: 'Current', text: 'Prototype development and hardware partnerships', active: true },
  { year: '2026–2027', text: 'Prototype validation, tissue phantom testing, initial funding secured' },
  { year: '2027–2028', text: 'Second-generation sensor integration, pre-clinical studies, clinical partnerships established' },
  { year: '2028–2029', text: 'Clinical trials, regulatory submissions (CE Mark, FDA)' },
  { year: '2030+', text: 'Commercial launch, transforming post-operative care worldwide' },
];

const JOIN = [
  { title: 'Investors', text: "We're raising our seed round to accelerate prototype development and secure key partnerships. Contact us to learn more." },
  { title: 'Clinical Partners', text: "We're seeking hospital partners for future clinical validation. If you're interested in early sepsis detection technology, let's talk." },
  { title: 'Talent', text: "We're building our founding team. If you're an expert in advanced sensing, biomedical engineering, clinical medicine, or medical device development, explore opportunities with us." },
  { title: 'Collaborators', text: 'We welcome partnerships with sensing research groups, medical device manufacturers, and healthcare innovators.' },
];

export default function About() {
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
            <Eyebrow>Our mission</Eyebrow>
          </FadeContent>
          <h1 className="font-rx100 mt-6">
            <SplitText
              text="About VitalQ"
              className="block text-5xl lg:text-7xl text-white"
              splitType="chars"
              delay={45}
              duration={0.9}
              textAlign="center"
            />
          </h1>
          <FadeContent delay={600} duration={1000}>
            <p className="mt-8 text-xl text-gray-300 leading-relaxed font-light">
              We're building the future of post-operative care. VitalQ is developing
              a wearable sensor that detects life-threatening complications hours
              before traditional methods — giving patients and clinicians the
              critical time needed for life-saving interventions.
            </p>
          </FadeContent>
        </div>
      </section>

      <Chapter num="01" title="The Problem We're Solving" tint>
        <p>
          Every year, 310 million surgical procedures are performed globally. Despite
          advances in surgical techniques, post-operative complications remain a
          leading cause of death, with sepsis being the deadliest — claiming 70% of
          patients who develop it.
        </p>
        <p>
          The fundamental challenge isn't treatment — it's timing. Current detection
          methods identify sepsis 12–18 hours too late, when the inflammatory
          cascade is already advanced and mortality risk is catastrophic. Every
          hour of delay increases mortality by 7–9%.
        </p>
        <p className="text-white font-medium">
          We believe patients deserve better. They deserve technology that sees
          complications coming before symptoms appear.
        </p>
      </Chapter>

      <Chapter num="02" title="Our Solution">
        <p>
          VitalQ pairs a proprietary optical sensing stack with advanced
          biosensing to continuously monitor the physiological signals that
          precede clinical deterioration — non-invasively, at the bedside or on
          the ward.
        </p>
        <p className="text-caladan-green font-semibold text-xl">
          The result: detection of sepsis 6–12 hours before clinical symptoms
          appear. That's enough time to save lives.
        </p>
        <p>
          Our wearable tracks multiple signals simultaneously and transmits them
          wirelessly, where our models identify early warning patterns. When risk
          is detected, clinicians receive immediate alerts, enabling early
          intervention that can reduce sepsis mortality from 70% to below 20%.
        </p>
      </Chapter>

      <Chapter num="03" title="Our Technology" tint>
        <p>
          We're keeping the specifics under wraps while we're in stealth. What we
          can say: a novel optical front end achieves sensitivity beyond
          conventional wearable sensors, and a signal-quality-gated pipeline keeps
          every score honest.
        </p>
        <p className="italic text-gray-400">
          We share the 'what' publicly. The 'how' stays private until launch.
        </p>
        <p>
          Our machine learning models, trained on large-scale patient data,
          correlate multi-signal patterns with deterioration risk in real time —
          designed for interpretability, low false-alarm rates, and real clinical
          workflows.
        </p>
        <p className="text-white font-medium">
          We're not just building a sensor. We're building a clinical decision
          support system that gives healthcare providers the information they
          need, when they need it most.
        </p>
      </Chapter>

      {/* Why Now — cards */}
      <section className="py-20 border-t border-zinc-800/60">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">04</span>
            <div className="mt-2"><Eyebrow /></div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">Why Now</h2>
            <p className="mt-6 text-gray-300 text-lg">
              Three critical technologies have converged to make VitalQ possible:
            </p>
          </FadeContent>
          <div className="mt-10 grid md:grid-cols-3 gap-5">
            {WHY_NOW.map((w, i) => (
              <FadeContent key={w.title} delay={i * 120} duration={900}>
                <SpotlightCard
                  className="h-full border-zinc-800/80 bg-zinc-950/80 card-lift"
                  spotlightColor={`rgba(${GREEN_RGB}, 0.12)`}
                >
                  <h3 className="text-white font-semibold leading-snug">{w.title}</h3>
                  <p className="mt-3 text-sm text-gray-400 leading-relaxed">{w.text}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
          <FadeContent delay={300}>
            <p className="mt-10 text-xl font-medium text-caladan-green">
              The technology is ready. The clinical need is urgent. The time is now.
            </p>
          </FadeContent>
        </div>
      </section>

      <Chapter num="05" title="Our Vision" tint>
        <p>VitalQ's technology platform extends far beyond post-operative sepsis.</p>
        <p>
          We envision a future where continuous, non-invasive biomarker monitoring
          is as routine as checking your heart rate — where diseases are detected
          at their earliest, most treatable stages, and where earlier detection
          saves lives every day.
        </p>
      </Chapter>

      {/* Team */}
      <section className="py-20 border-t border-zinc-800/60">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">06</span>
            <div className="mt-2"><Eyebrow /></div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">Our Team</h2>
          </FadeContent>
          <div className="mt-12 grid grid-cols-1 md:grid-cols-2 gap-5 max-w-3xl">
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

      {/* Advisors */}
      <section className="py-20 border-t border-zinc-800/60 bg-zinc-950/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8 text-center">
          <FadeContent>
            <h2 className="text-3xl font-rx100 text-white">Our Advisors</h2>
            <p className="mt-6 text-gray-300 text-lg">
              VitalQ is supported by world-class research in advanced sensing,
              clinical medicine, and medical device development.
            </p>
            <p className="mt-4 text-gray-300 text-lg">
              We're building a world-class advisory board. If you're interested in
              advising VitalQ, we'd love to hear from you.
            </p>
          </FadeContent>
        </div>
      </section>

      {/* Journey timeline */}
      <section className="py-20 border-t border-zinc-800/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <h2 className="text-3xl font-rx100 text-white tracking-tight text-center">Our Journey</h2>
          </FadeContent>
          <div className="mt-12 space-y-6 text-gray-300 text-lg relative before:absolute before:inset-0 before:ml-5 before:-translate-x-px md:before:mx-auto md:before:translate-x-0 before:h-full before:w-0.5 before:bg-gradient-to-b before:from-transparent before:via-caladan-green/40 before:to-transparent">
            {TIMELINE.map((t, i) => (
              <FadeContent key={t.year} delay={i * 100} duration={800}>
                <div className="relative flex items-center justify-between md:justify-normal md:odd:flex-row-reverse group">
                  <div
                    className={`flex items-center justify-center w-10 h-10 rounded-full border bg-zinc-950 shrink-0 md:order-1 md:group-odd:-translate-x-1/2 md:group-even:translate-x-1/2 z-10 ${
                      t.active ? 'border-caladan-green/60' : 'border-zinc-700'
                    }`}
                  >
                    {t.active && <div className="w-2 h-2 bg-caladan-green rounded-full" />}
                  </div>
                  <div
                    className={`w-[calc(100%-4rem)] md:w-[calc(50%-2.5rem)] p-4 rounded-xl border card-lift ${
                      t.active
                        ? 'bg-zinc-950 border-caladan-green/25 text-right md:text-left md:group-even:text-right'
                        : 'bg-zinc-950/50 border-zinc-800 text-right md:text-left md:group-even:text-right'
                    }`}
                  >
                    <span className="font-rx100 text-caladan-green block mb-1 text-sm">{t.year}</span>
                    <p className="text-sm text-gray-300">{t.text}</p>
                  </div>
                </div>
              </FadeContent>
            ))}
          </div>
        </div>
      </section>

      {/* Join Us */}
      <section className="py-24 border-t border-zinc-800/60 bg-zinc-950/60">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <h2 className="text-3xl md:text-4xl font-rx100 text-white">Join Us</h2>
            <p className="mt-6 text-gray-300 text-lg max-w-2xl">
              We're at the beginning of something transformative. If you're
              passionate about building technology that saves lives, we want to
              hear from you.
            </p>
          </FadeContent>
          <div className="mt-10 grid sm:grid-cols-2 gap-5">
            {JOIN.map((j, i) => (
              <FadeContent key={j.title} delay={i * 100} duration={900}>
                <SpotlightCard
                  className="h-full border-zinc-800/80 bg-zinc-950/80 card-lift"
                  spotlightColor={`rgba(${GREEN_RGB}, 0.12)`}
                >
                  <h4 className="text-white font-semibold flex items-center gap-2">
                    <span className="eyebrow-dot" />
                    {j.title}
                  </h4>
                  <p className="text-gray-400 text-sm mt-3 leading-relaxed">{j.text}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
          <FadeContent delay={300}>
            <Link
              to="/contact"
              className="group inline-flex items-center gap-2 mt-12 bg-caladan-green text-black font-semibold px-8 py-4 rounded-full text-sm tracking-wide hover:bg-white transition-colors duration-300"
            >
              Get in touch
              <ArrowRight className="w-4 h-4 transition-transform duration-300 group-hover:translate-x-1" />
            </Link>
          </FadeContent>
        </div>
      </section>
    </div>
  );
}
