import React from 'react';
import { Link } from 'react-router-dom';
import { FileDown, FileText, ArrowUpRight } from 'lucide-react';
import FadeContent from '../components/ui/FadeContent';
import SpotlightCard from '../components/ui/SpotlightCard';

const PDF_URL = '/vitalquant-paper.pdf';

function Eyebrow({ children }) {
  return (
    <span className="eyebrow">
      <span className="eyebrow-dot" />
      {children}
    </span>
  );
}

const FINDINGS = [
  {
    metric: '0.12 bpm',
    label: 'PPG heart-rate MAE',
    body: 'v2 chain recovers 65.00 ± 0.21 bpm against 65 bpm synthetic ground truth across 10 s windows.',
  },
  {
    metric: '2.72× → 1.14×',
    label: 'Squeezed-readout advantage',
    body: 'SNR advantage collapses as proportional tissue noise dominates — squeezing is a conditional instrument, not a multiplier.',
  },
  {
    metric: '100%',
    label: 'Corruption attribution',
    body: 'Every low-SQI window is rejected through a named physical mechanism (rail or flat fraction = 1.0), not an opaque score.',
  },
  {
    metric: '7 schemas',
    label: 'Provenance-first data model',
    body: 'raw / clean / quality / features / ml / config / meta — with a data_class tag on every row: real, synthetic, or simulated.',
  },
];

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

export default function Paper() {
  return (
    <div className="pt-24">
      {/* Hero */}
      <section className="py-16">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
          <FadeContent>
            <Eyebrow>Research · Preprint · October 2026</Eyebrow>
            <h1 className="mt-6 text-4xl md:text-5xl font-rx100 text-white tracking-tight leading-tight">
              VitalQuant: A Multimodal Wearable Sensing Platform with Quantum-Inspired Readout
              Modelling
            </h1>
            <p className="mt-4 text-lg text-gray-400">
              VitalQuant Research — Riyansh Diwan, Moses Man, Andre Law · London, United Kingdom
            </p>
            <p className="mt-6 text-gray-300 leading-relaxed text-lg">
              Our full technical paper: the measurement platform itself as the research object.
              Hardware and firmware, the idempotent ingest protocol, the provenance-first data
              model, the SQI-gated processing stack, the anomaly-detection layer, and a quantum
              readout simulator that reports where the physics helps — and where it does not.
            </p>
            <div className="mt-8 flex flex-wrap gap-4">
              <a
                href={PDF_URL}
                target="_blank"
                rel="noreferrer"
                className="inline-flex items-center gap-2 rounded-full bg-caladan-green px-6 py-3 text-sm font-semibold text-caladan-dark transition hover:brightness-110"
              >
                <FileText className="h-4 w-4" /> Read the paper
              </a>
              <a
                href={PDF_URL}
                download="VitalQuant_paper.pdf"
                className="inline-flex items-center gap-2 rounded-full border border-zinc-700 px-6 py-3 text-sm font-semibold text-gray-200 transition hover:border-caladan-green hover:text-caladan-green"
              >
                <FileDown className="h-4 w-4" /> Download PDF
              </a>
              <a
                href="https://github.com/Riyu-D1/VitalQuant"
                target="_blank"
                rel="noreferrer"
                className="inline-flex items-center gap-2 rounded-full border border-zinc-700 px-6 py-3 text-sm font-semibold text-gray-200 transition hover:border-caladan-green hover:text-caladan-green"
              >
                Source code <ArrowUpRight className="h-4 w-4" />
              </a>
            </div>
          </FadeContent>
        </div>
      </section>

      {/* Key findings */}
      <Section num="01" title="Headline results, measured not claimed">
        <p>
          Every number in the paper is an executed output of the platform's own code — the quantum
          experiment matrix, the synthetic device harness, and the test suite — not an external
          benchmark.
        </p>
        <div className="grid gap-4 md:grid-cols-2 pt-4">
          {FINDINGS.map((f) => (
            <SpotlightCard key={f.label} className="p-6">
              <div className="text-2xl font-rx100 text-caladan-green">{f.metric}</div>
              <div className="mt-2 text-sm font-semibold uppercase tracking-[0.15em] text-white">
                {f.label}
              </div>
              <p className="mt-2 text-sm text-gray-400 leading-relaxed">{f.body}</p>
            </SpotlightCard>
          ))}
        </div>
      </Section>

      {/* Embedded PDF */}
      <Section num="02" title="Read it here" tint>
        <p>
          8 pages · ~5,300 words · 19 references. If the viewer below does not render in your
          browser, use the download button above.
        </p>
        <FadeContent delay={200}>
          <div className="mt-8 rounded-2xl border border-zinc-800 bg-zinc-950 overflow-hidden">
            <object
              data={`${PDF_URL}#view=FitH`}
              type="application/pdf"
              className="w-full h-[85vh]"
            >
              <div className="p-10 text-center text-gray-400">
                <p>Your browser can't display the PDF inline.</p>
                <a
                  href={PDF_URL}
                  target="_blank"
                  rel="noreferrer"
                  className="mt-4 inline-flex items-center gap-2 text-caladan-green hover:underline"
                >
                  Open it directly <ArrowUpRight className="h-4 w-4" />
                </a>
              </div>
            </object>
          </div>
        </FadeContent>
      </Section>

      {/* Abstract */}
      <Section num="03" title="Abstract">
        <p className="text-gray-300">
          Sepsis and post-operative physiological deterioration remain among the deadliest
          time-critical conditions in medicine: each hour of delayed treatment measurably increases
          mortality, yet standard monitoring detects deterioration only after symptoms are florid.
          We present <span className="text-white font-semibold">VitalQuant</span>, a research
          platform for multimodal wearable physiological sensing built around a single design rule
          — <em className="text-caladan-green not-italic">measurement honesty before clinical claims</em>.
        </p>
        <p>
          The system integrates a custom multi-sensor wearable design and ESP32 firmware with
          provenance-first timestamping; an idempotent store-and-forward ingest API with device
          authentication and clock correction; a partitioned data architecture tagging every row as
          real, synthetic, or simulated; a versioned SQI-gated processing stack; a label-free
          anomaly layer with per-subject baselines; and a quantum-inspired readout simulator with a
          composable noise hierarchy.
        </p>
        <p>
          VitalQuant is released source-available as a scientific artefact: a research instrument,
          not a medical device.
        </p>
        <p>
          <Link to="/model" className="text-caladan-green hover:underline">
            Read about LAT-D-VQt-1, our clinical decision model →
          </Link>
        </p>
      </Section>
    </div>
  );
}
