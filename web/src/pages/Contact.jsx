import React from 'react';
import { ArrowRight } from 'lucide-react';
import SplitText from '../components/ui/SplitText';
import FadeContent from '../components/ui/FadeContent';
import SpotlightCard from '../components/ui/SpotlightCard';

const inputCls =
  'w-full bg-caladan-dark border border-zinc-800 rounded-lg px-4 py-3 text-white placeholder:text-zinc-600 focus:outline-none focus:border-caladan-green focus:ring-1 focus:ring-caladan-green transition-colors';

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
            Interested in bringing quantum photonics to your organization? Drop us
            a line below.
          </p>
        </FadeContent>

        <FadeContent delay={300} duration={900}>
          <SpotlightCard
            className="border-zinc-800/80 bg-zinc-950/80"
            spotlightColor="rgba(20, 200, 113, 0.10)"
          >
            <form action="https://formsubmit.co/riyanshdiwan@gmail.com" method="POST" className="space-y-6">
              {/* FormSubmit Configuration Options */}
              <input type="hidden" name="_subject" value="New Website Contact Form Submission!" />
              <input type="hidden" name="_template" value="table" />

              <h2 className="text-2xl font-rx100 text-white mb-2">Send a Message</h2>
              <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
                <div>
                  <label htmlFor="firstName" className="block text-sm font-medium text-gray-400 mb-2">First Name</label>
                  <input type="text" id="firstName" name="firstName" className={inputCls} placeholder="John" />
                </div>
                <div>
                  <label htmlFor="lastName" className="block text-sm font-medium text-gray-400 mb-2">Last Name</label>
                  <input type="text" id="lastName" name="lastName" className={inputCls} placeholder="Doe" />
                </div>
              </div>
              <div>
                <label htmlFor="email" className="block text-sm font-medium text-gray-400 mb-2">Email Address</label>
                <input type="email" id="email" name="email" className={inputCls} placeholder="john@example.com" />
              </div>
              <div>
                <label htmlFor="message" className="block text-sm font-medium text-gray-400 mb-2">Message</label>
                <textarea id="message" name="message" rows="4" className={inputCls} placeholder="How can we help?"></textarea>
              </div>
              <button
                type="submit"
                className="group w-full inline-flex items-center justify-center gap-2 bg-caladan-green text-black px-8 py-4 rounded-full font-semibold tracking-wide hover:bg-white transition-colors duration-300 text-sm"
              >
                Send message
                <ArrowRight className="w-4 h-4 transition-transform duration-300 group-hover:translate-x-1" />
              </button>
            </form>
          </SpotlightCard>
        </FadeContent>
      </div>
    </div>
  );
}
