import React from 'react';
import { Mail, MapPin, ArrowRight, FileText, Code2 } from 'lucide-react';
import SplitText from '../components/ui/SplitText';
import FadeContent from '../components/ui/FadeContent';
import SpotlightCard from '../components/ui/SpotlightCard';

const EMAIL = 'hello@vitalquant.tech';

export default function Contact() {
  return (
    <div className="pt-32 pb-24 px-6 lg:px-8 max-w-7xl mx-auto min-h-screen">
      <div className="max-w-3xl mx-auto">
        <FadeContent>
          <span className="eyebrow">
            <span className="eyebrow-dot" />
            Contact
          </span>
        </FadeContent>
        <h1 className="font-rx100 mt-4">
          <SplitText
            text="Let's talk."
            className="block text-4xl md:text-6xl text-white tracking-tight"
            splitType="chars"
            delay={45}
            duration={0.9}
            textAlign="left"
          />
        </h1>
        <FadeContent delay={500} duration={900}>
          <p className="text-gray-400 text-lg mt-4 mb-12">
            Research collaborations, clinical partners, investors — or just a question
            about the platform. One real inbox, read by the founders.
          </p>
        </FadeContent>

        <FadeContent delay={300} duration={900}>
          <SpotlightCard
            className="border-zinc-800/80 bg-zinc-950/80 p-8 md:p-10"
            spotlightColor="rgba(20, 200, 113, 0.10)"
          >
            <div className="flex items-center gap-3">
              <Mail className="h-6 w-6 text-caladan-green" />
              <h2 className="text-2xl font-rx100 text-white">Email us</h2>
            </div>
            <a
              href={`mailto:${EMAIL}`}
              className="mt-4 block text-2xl md:text-3xl font-rx100 text-caladan-green hover:text-white transition-colors"
            >
              {EMAIL}
            </a>
            <p className="mt-4 text-sm text-gray-400 leading-relaxed">
              Tell us who you are and what you're interested in — clinical validation,
              the sensing platform, the model, or investment. We reply from this address,
              so it's a conversation, not a form.
            </p>
            <a
              href={`mailto:${EMAIL}?subject=VitalQuant%20enquiry`}
              className="group mt-8 inline-flex items-center gap-2 bg-caladan-green text-black px-8 py-4 rounded-full font-semibold tracking-wide hover:bg-white transition-colors duration-300 text-sm"
            >
              Email VitalQuant
              <ArrowRight className="w-4 h-4 transition-transform duration-300 group-hover:translate-x-1" />
            </a>
          </SpotlightCard>
        </FadeContent>

        <FadeContent delay={450} duration={900}>
          <div className="mt-10 grid sm:grid-cols-3 gap-4">
            <div className="rounded-xl border border-zinc-800 bg-zinc-950/60 p-5">
              <MapPin className="h-4 w-4 text-caladan-green" />
              <p className="mt-3 text-xs uppercase tracking-[0.2em] text-gray-500">Based</p>
              <p className="mt-1 text-sm text-gray-300">London, United Kingdom</p>
            </div>
            <a
              href="/paper"
              className="rounded-xl border border-zinc-800 bg-zinc-950/60 p-5 hover:border-caladan-green/50 transition-colors"
            >
              <FileText className="h-4 w-4 text-caladan-green" />
              <p className="mt-3 text-xs uppercase tracking-[0.2em] text-gray-500">Research</p>
              <p className="mt-1 text-sm text-gray-300">Read the paper first</p>
            </a>
            <a
              href="https://github.com/Riyu-D1/VitalQuant"
              target="_blank"
              rel="noreferrer"
              className="rounded-xl border border-zinc-800 bg-zinc-950/60 p-5 hover:border-caladan-green/50 transition-colors"
            >
              <Code2 className="h-4 w-4 text-caladan-green" />
              <p className="mt-3 text-xs uppercase tracking-[0.2em] text-gray-500">Code</p>
              <p className="mt-1 text-sm text-gray-300">Source on GitHub</p>
            </a>
          </div>
        </FadeContent>
      </div>
    </div>
  );
}
