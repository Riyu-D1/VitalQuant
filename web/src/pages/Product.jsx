import React from 'react';
import { Link } from 'react-router-dom';
import {
  Cpu,
  Radio,
  Thermometer,
  Activity,
  Waves,
  Hand,
  Wifi,
  Clock,
  Server,
  Database,
  ShieldCheck,
  GitBranch,
  BrainCircuit,
  Atom,
  ArrowUpRight,
  FileText,
} from 'lucide-react';
import SplitText from '../components/ui/SplitText';
import FadeContent from '../components/ui/FadeContent';
import SpotlightCard from '../components/ui/SpotlightCard';

const GREEN_RGB = '20, 200, 113';
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

const SENSORS = [
  {
    icon: Activity,
    name: 'PPG — dual wavelength',
    part: 'MAX86141 (v1) · MAX30102 (v0)',
    detail:
      '660 nm red + 880 nm IR channels at 200 Hz with ambient-light cancellation. The primary cardiovascular channel — heart rate, PRV, perfusion index, R ratio.',
  },
  {
    icon: Waves,
    name: '11-channel spectral',
    part: 'AS7341-DLGM',
    detail:
      'F1–F8 + clear + NIR + flicker, 2 Hz frames. Reads the ambient light field so downstream processing can tell physiology from illumination.',
  },
  {
    icon: Thermometer,
    name: 'Contact temperature',
    part: 'MLX90637 (v1) · MLX90614 (v0)',
    detail: 'Object and ambient temperature at 1 Hz — continuous skin-temperature trends, not spot readings.',
  },
  {
    icon: Radio,
    name: 'Environment',
    part: 'BME280',
    detail:
      'Air temperature, humidity and pressure at 0.5 Hz. Context channel — lets the system separate patient change from room change.',
  },
  {
    icon: Cpu,
    name: '6-axis motion',
    part: 'ICM-42670-P (v1) · MPU6050 (v0)',
    detail:
      'Accelerometer + gyro at 52 Hz. Motion is a first-class channel: it gates signal quality so artefact never silently becomes data.',
  },
  {
    icon: Hand,
    name: 'Contact force',
    part: 'FSR402',
    detail:
      'Analog pressure level at 20 Hz. Tells the pipeline whether the sensor is actually on skin — an honest "no signal" beats a bad one.',
  },
];

const STACK = [
  {
    icon: Cpu,
    title: 'Firmware (ESP32)',
    body: 'Sensor presence, buses, rates and optics live in a per-revision YAML hardware profile — config, never compiled code. NTP discipline with a fitted clock model, a 30-minute flash ring buffer, and store-and-forward bursts when the link drops.',
  },
  {
    icon: Wifi,
    title: 'Ingest API (FastAPI)',
    body: 'One device write path: POST /v1/ingest/batch. Per-device 256-bit keys hashed at rest, idempotent batch_id replay protection, async 202 ACK semantics, and an explicit error taxonomy (auth.*, batch.replay, schema.*, clock.*).',
  },
  {
    icon: Clock,
    title: 'Clock correction',
    body: 'Samples keep device-side timestamps; the server fits per-session offset and drift (ppm) and converts to UTC with batch-local correction — both raw and corrected times are stored. Provenance over prettiness.',
  },
  {
    icon: GitBranch,
    title: 'Processing pipeline',
    body: 'Staged SQI-gated stack: per-channel signal-quality indices (skew, kurtosis, template correlation, spectral purity, clip fraction), motion-gated fusion, windowed features (HR, PRV/RMSSD, perfusion index, R ratio). Bad windows are abstained from — never imputed.',
  },
  {
    icon: Database,
    title: 'Data model',
    body: 'Seven Postgres schemas — raw / clean / quality / features / ml / config / meta — with native partitioning and a data_class tag on every row: real, synthetic, or simulated. Nothing enters ml.* without provenance.',
  },
  {
    icon: ShieldCheck,
    title: 'Honesty by design',
    body: 'The platform tags which mechanism rejected a window (rail, flat, motion) instead of emitting an opaque score. Synthetic devices and a corruption battery exist precisely so quality claims are measured, not asserted.',
  },
];

const MODEL_POINTS = [
  {
    value: 'Wearable-only',
    label: 'input contract',
    text: 'Every feature the model consumes is a channel the device can measure at the bedside. No invasive-only signals, ever.',
  },
  {
    value: 'Calibrated',
    label: 'confidence',
    text: 'Answers ship with confidence honest enough to act on selectively — and to abstain when the signal is not there.',
  },
  {
    value: 'Label-free',
    label: 'anomaly layer',
    text: 'Per-subject rolling baselines flag deviation without labelled clinical events — the level of method matches the level of data.',
  },
];

const LIMITS = [
  'Not a medical device. Outputs are experimental anomaly and decision scores, not a diagnosis or a treatment recommendation.',
  'Not clinically validated. Nothing on this page has been through regulatory review.',
  'Not a chatbot. LAT-D-VQt-1 answers a fixed clinical question set — it does not take arbitrary requests.',
  'Not magic. Every quality and advantage number we publish is an executed output of our own pipeline.',
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
              VitalQuant is a multimodal wearable research platform — custom sensing hardware,
              a provenance-first data pipeline, and a clinical decision model — built to catch
              physiological deterioration earlier than conventional monitoring. Everything
              below is what the repo actually builds.
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
                to="/paper"
                className="inline-flex items-center gap-2 rounded-full border border-zinc-700 px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-gray-300 transition-colors hover:border-caladan-green hover:text-caladan-green"
              >
                <FileText size={16} />
                Read the paper
              </Link>
            </div>
          </FadeContent>
        </div>
      </section>

      {/* Hardware */}
      <section className="py-20 border-t border-zinc-800/60 bg-zinc-950/60">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">01</span>
            <div className="mt-2">
              <Eyebrow />
            </div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">
              The wearable
            </h2>
            <p className="mt-6 text-gray-300 leading-relaxed text-lg max-w-3xl">
              Two hardware revisions, one contract: six sensor modalities fused on one ESP32
              node. <span className="text-white font-medium">hw_v0</span> is the breakout rig
              that physically exists; <span className="text-white font-medium">hw_v1</span> is
              the custom-PCB target. Sensor presence, rates and optics are declared in YAML
              profiles — validated against a schema, never baked into firmware.
            </p>
          </FadeContent>
          <div className="mt-10 grid gap-6 md:grid-cols-2">
            {SENSORS.map((s, i) => (
              <FadeContent key={s.name} delay={i * 100}>
                <SpotlightCard className="h-full p-6" spotlightColor={`rgba(${GREEN_RGB}, 0.15)`}>
                  <div className="flex items-center gap-3">
                    <s.icon className="h-5 w-5 text-caladan-green" />
                    <p className="font-rx100 text-lg text-white">{s.name}</p>
                  </div>
                  <p className="mt-1 text-xs uppercase tracking-[0.2em] text-caladan-green">
                    {s.part}
                  </p>
                  <p className="mt-3 text-sm text-gray-400 leading-relaxed">{s.detail}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
        </div>
      </section>

      {/* Software */}
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
              From device flash to researcher dashboard: an idempotent ingest API, a
              clock-corrected processing pipeline, and a data model that refuses to lose
              provenance. Runs end-to-end locally with a synthetic device — no hardware
              required to develop against it.
            </p>
          </FadeContent>
          <div className="mt-10 grid gap-6 md:grid-cols-2">
            {STACK.map((s, i) => (
              <FadeContent key={s.title} delay={i * 100}>
                <SpotlightCard className="h-full p-6" spotlightColor={`rgba(${GREEN_RGB}, 0.15)`}>
                  <div className="flex items-center gap-3">
                    <s.icon className="h-5 w-5 text-caladan-green" />
                    <p className="font-rx100 text-lg text-white">{s.title}</p>
                  </div>
                  <p className="mt-3 text-sm text-gray-400 leading-relaxed">{s.body}</p>
                </SpotlightCard>
              </FadeContent>
            ))}
          </div>
        </div>
      </section>

      {/* Custom model */}
      <section className="py-20 border-t border-zinc-800/60 bg-zinc-950/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <span className="text-xs font-rx100 text-zinc-600">03</span>
            <div className="mt-2">
              <Eyebrow />
            </div>
            <h2 className="mt-4 text-3xl md:text-4xl font-rx100 text-white tracking-tight">
              LAT-D-VQt-1 — the custom model
            </h2>
          </FadeContent>
          <FadeContent delay={150}>
            <div className="mt-6 text-gray-300 leading-relaxed space-y-4 text-lg">
              <p>
                On top of the pipeline sits <span className="text-white font-medium">LAT-D-VQt-1</span>,
                a closed-weight clinical decision model built for VitalQuant by LatticeAG.
                To be precise about what it is: not a general-purpose LLM — a post-trained
                time-series decision model that answers a fixed set of deterioration questions
                from wearable channels only.
              </p>
              <p>
                Training and evaluation were run by LatticeAG; the weights are held by
                VitalQuant. Every input it sees is something the device can measure
                continuously — a model that quietly depends on a channel the hardware cannot
                supply is useless in the setting it was designed for.
              </p>
            </div>
          </FadeContent>
          <div className="mt-10 grid gap-6 md:grid-cols-3">
            {MODEL_POINTS.map((f, i) => (
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
              <a
                href={LATTICEAG_MODEL_URL}
                target="_blank"
                rel="noreferrer"
                className="inline-flex items-center gap-2 rounded-full border border-zinc-700 px-6 py-3 text-sm font-semibold text-gray-200 transition hover:border-caladan-green hover:text-caladan-green"
              >
                Technical writeup <ArrowUpRight className="h-4 w-4" />
              </a>
            </div>
          </FadeContent>
        </div>
      </section>

      {/* Quantum-inspired readout */}
      <Section num="04" title="Quantum-inspired readout — with honest bounds">
        <p>
          The platform ships a readout simulator with a composable noise hierarchy —
          photon shot noise, tissue scattering, electronics and ambient terms — so we can
          ask whether quantum-inspired detection actually helps <em>our</em> signal, in
          <em> our</em> noise regime.
        </p>
        <p>
          The measured answer is deliberately unspectacular: squeezed-state readout gives a
          <span className="text-white font-medium"> 2.72× SNR advantage where shot noise dominates,
          collapsing to 1.14×</span> once proportional tissue noise takes over. We publish the
          boundary, not the hype — squeezing is a conditional instrument, not a free
          multiplier.
        </p>
        <div className="flex items-center gap-3 pt-2">
          <Atom className="h-5 w-5 text-caladan-green" />
          <span className="text-sm text-gray-400">
            Full noise model and experiment matrix in the research paper.
          </span>
        </div>
      </Section>

      {/* Honesty */}
      <Section num="05" title="What it is not" tint>
        <ul className="space-y-3">
          {LIMITS.map((l) => (
            <li key={l} className="flex gap-3">
              <span className="mt-2 h-1.5 w-1.5 flex-none rounded-full bg-caladan-green/70" />
              <span>{l}</span>
            </li>
          ))}
        </ul>
      </Section>

      {/* CTA */}
      <section className="py-20 border-t border-zinc-800/60">
        <div className="max-w-4xl mx-auto px-6 lg:px-8 text-center">
          <FadeContent>
            <Server className="h-8 w-8 text-caladan-green mx-auto" />
            <h2 className="mt-4 font-rx100 text-3xl md:text-4xl text-white tracking-tight">
              See the whole stack working
            </h2>
            <p className="mt-4 text-gray-400 text-lg">
              The paper documents the platform end-to-end — hardware profiles, ingest
              protocol, processing chain, model layer and the quantum experiment — with
              every number measured by running the code.
            </p>
            <div className="mt-8 flex flex-wrap items-center justify-center gap-4">
              <Link
                to="/paper"
                className="inline-flex items-center gap-2 rounded-full bg-caladan-green px-7 py-3 text-sm font-semibold uppercase tracking-[0.15em] text-black transition-colors hover:bg-white"
              >
                <FileText size={16} />
                Read the paper
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
