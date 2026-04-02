import React from 'react';

export default function About() {
  return (
    <>
      {/* Header Section */}
      <section className="pt-48 pb-20 bg-caladan-dark text-center">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
            <h1 className="hero-title font-rx100 text-5xl lg:text-7xl font-bold text-white mb-6">About VitalQ</h1>
            <p className="text-xl text-gray-300 leading-relaxed font-light">
                We're building the future of post-operative care. VitalQ is developing the world's first quantum-enhanced wearable sensor that detects life-threatening complications hours before traditional methods—giving patients and clinicians the critical time needed for life-saving interventions.
            </p>
        </div>
      </section>

      {/* The Problem Section */}
      <section className="py-20 bg-zinc-900 border-t border-gray-800">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
            <h2 className="text-3xl font-bold text-white mb-6 tracking-tight">The Problem We're Solving</h2>
            <div className="text-gray-300 leading-relaxed space-y-4 text-lg">
                <p>Every year, 310 million surgical procedures are performed globally. Despite advances in surgical techniques, post-operative complications remain a leading cause of death, with sepsis being the deadliest—claiming 70% of patients who develop it.</p>
                <p>The fundamental challenge isn't treatment—it's timing. Current detection methods identify sepsis 12-18 hours too late, when the inflammatory cascade is already advanced and mortality risk is catastrophic. Every hour of delay increases mortality by 7-9%.</p>
                <p className="text-white font-medium">We believe patients deserve better. They deserve technology that sees complications coming before symptoms appear.</p>
            </div>
        </div>
      </section>

      {/* Our Solution Section */}
      <section className="py-20 bg-caladan-dark border-t border-gray-800">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
            <h2 className="text-3xl font-bold text-white mb-6 tracking-tight">Our Solution</h2>
            <div className="text-gray-300 leading-relaxed space-y-4 text-lg">
                <p>VitalQ combines quantum photonics with advanced biosensing to continuously monitor inflammatory and metabolic biomarkers in real-time. Using squeezed light—a quantum optics technique that reduces measurement noise below classical limits—our sensor achieves unprecedented sensitivity.</p>
                <p className="text-caladan-green font-semibold text-xl">The result: detection of sepsis 6-12 hours before clinical symptoms appear. That's enough time to save lives.</p>
                <p>Our wearable sensor tracks multiple biomarkers simultaneously—IL-6, lactate, pH, and tissue perfusion—transmitting data wirelessly to the cloud where our AI algorithms identify early warning patterns. When risk is detected, clinicians receive immediate alerts, enabling early intervention that can reduce sepsis mortality from 70% to below 20%.</p>
            </div>
        </div>
      </section>

      {/* Our Technology Section */}
      <section className="py-20 bg-zinc-900 border-t border-gray-800">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
            <h2 className="text-3xl font-bold text-white mb-6 tracking-tight">Our Technology</h2>
            <div className="text-gray-300 leading-relaxed space-y-4 text-lg">
                <p>At the heart of VitalQ is quantum-enhanced optical sensing. We leverage integrated photonics—specifically thin-film lithium niobate chips—to generate squeezed light states that improve signal-to-noise ratios by 2-5× compared to classical sensors.</p>
                <p className="italic text-gray-400">This isn't quantum computing hype. It's proven physics applied to an urgent clinical problem.</p>
                <p>Multi-wavelength spectroscopy, including Raman and near-infrared absorption, allows us to measure biomarkers non-invasively through the skin. Our machine learning algorithms, trained on tens of thousands of patient records, correlate biomarker patterns with sepsis risk in real-time.</p>
                <p className="text-white font-medium">We're not just building a sensor. We're building a clinical decision support system that gives healthcare providers the information they need, when they need it most.</p>
            </div>
        </div>
      </section>

      {/* Why Now Section */}
      <section className="py-20 bg-caladan-dark border-t border-gray-800">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
            <h2 className="text-3xl font-bold text-white mb-6 tracking-tight">Why Now</h2>
            <p className="text-gray-300 mb-6 text-lg">Three critical technologies have converged to make VitalQ possible:</p>
            <ul className="list-disc list-inside text-gray-300 space-y-4 text-lg mb-8 ml-4">
                <li><strong className="text-white">Quantum photonics miniaturization:</strong> Integrated photonic chips can now generate squeezed light on devices smaller than 1 cm²—a breakthrough that occurred in the last 3-5 years.</li>
                <li><strong className="text-white">Advanced AI and machine learning:</strong> Modern algorithms can detect subtle patterns in multivariate biomarker data that would be invisible to human observation.</li>
                <li><strong className="text-white">Clinical data availability:</strong> Large-scale patient datasets enable us to train and validate our models with unprecedented accuracy.</li>
            </ul>
            <p className="text-xl font-medium text-caladan-green">The technology is ready. The clinical need is urgent. The time is now.</p>
        </div>
      </section>

      {/* Our Vision Section */}
      <section className="py-20 bg-zinc-900 border-t border-gray-800">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
            <h2 className="text-3xl font-bold text-white mb-6 tracking-tight">Our Vision</h2>
            <p className="text-gray-300 mb-6 text-lg">VitalQ's technology platform extends far beyond post-operative sepsis. The same quantum-enhanced optical sensing that detects early inflammation can be applied to:</p>
            <ul className="list-disc list-inside text-gray-300 space-y-2 text-lg mb-8 ml-4 grid grid-cols-1 md:grid-cols-2 gap-x-4">
                <li>Cancer biomarker monitoring</li>
                <li>Diabetes and metabolic disease tracking</li>
                <li>Cardiovascular risk assessment</li>
                <li>Chronic inflammatory conditions</li>
                <li>Remote patient monitoring for vulnerable populations</li>
            </ul>
            <p className="text-gray-300 leading-relaxed text-lg">
               We envision a future where continuous, non-invasive biomarker monitoring is as routine as checking your heart rate—where diseases are detected at their earliest, most treatable stages, and where quantum technology saves lives every day.
            </p>
        </div>
      </section>

      {/* Our Team Section */}
      <section className="py-20 bg-caladan-dark border-t border-gray-800">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
            <div className="text-center mb-16">
                <h2 className="text-4xl font-bold text-white mb-6">Our Team</h2>
            </div>
            
            <div className="grid grid-cols-1 md:grid-cols-2 gap-12">
                <div className="bg-zinc-900 rounded-3xl p-8 border border-gray-800 shadow-lg">
                    <div className="w-20 h-20 rounded-full bg-gray-800 mb-6 flex items-center justify-center text-gray-300 font-bold text-2xl border border-gray-700">
                        RD
                    </div>
                    <h3 className="text-2xl font-bold text-white mb-1">Riyansh Diwan</h3>
                    <p className="text-caladan-green text-sm font-semibold uppercase tracking-wider mb-6">Co-Founder & CEO</p>
                    <div className="text-gray-300 text-sm space-y-4">
                        <p>Visionary leader bridging the gap between advanced sensors and practical clinical deployments.</p>
                    </div>
                </div>

                <div className="bg-zinc-900 rounded-3xl p-8 border border-gray-800 shadow-lg">
                    <div className="w-20 h-20 rounded-full bg-gray-800 mb-6 flex items-center justify-center text-gray-300 font-bold text-2xl border border-gray-700">
                        NP
                    </div>
                    <h3 className="text-2xl font-bold text-white mb-1">Naga Perla</h3>
                    <p className="text-caladan-green text-sm font-semibold uppercase tracking-wider mb-6">Co-Founder & CTO</p>
                    <div className="text-gray-300 text-sm space-y-4">
                        <p>Expert engineer directing the development of quantum-based patient tracking solutions.</p>
                    </div>
                </div>
            </div>
        </div>
      </section>

      {/* Advisors Section */}
      <section className="py-20 bg-zinc-900 border-t border-gray-800">
        <div className="max-w-4xl mx-auto px-6 lg:px-8 text-center">
            <h2 className="text-3xl font-bold text-white mb-6 tracking-tight">Our Advisors</h2>
            <p className="text-gray-300 mb-6 text-lg">VitalQ is supported by world-class advisors in quantum photonics, clinical medicine, and medical device development. Our advisory board includes:</p>
            <ul className="text-gray-300 space-y-2 text-lg mb-8">
                <li>Leading experts in quantum optics from top research institutions</li>
                <li>Clinical advisors in critical care and surgical medicine</li>
                <li>Medical device regulatory consultants</li>
            </ul>
            <p className="text-gray-400 italic text-sm">
                We're building a world-class advisory board... If you're interested in advising VitalQ, we'd love to hear from you.
            </p>
        </div>
      </section>

      {/* Our Journey Section */}
      <section className="py-20 bg-caladan-dark border-t border-gray-800">
        <div className="max-w-4xl mx-auto px-6 lg:px-8">
            <h2 className="text-3xl font-bold text-white mb-10 tracking-tight text-center">Our Journey</h2>
            <div className="space-y-6 text-gray-300 text-lg relative before:absolute before:inset-0 before:ml-5 before:-translate-x-px md:before:mx-auto md:before:translate-x-0 before:h-full before:w-0.5 before:bg-gradient-to-b before:from-transparent before:via-gray-700 before:to-transparent">
                <div className="relative flex items-center justify-between md:justify-normal md:odd:flex-row-reverse group is-active">
                    <div className="flex items-center justify-center w-10 h-10 rounded-full border border-gray-700 bg-zinc-900 text-caladan-green shrink-0 md:order-1 md:group-odd:-translate-x-1/2 md:group-even:translate-x-1/2 shadow-sm z-10">
                        <div className="w-2 h-2 bg-caladan-green rounded-full"></div>
                    </div>
                    <div className="w-[calc(100%-4rem)] md:w-[calc(50%-2.5rem)] bg-zinc-900 p-4 rounded-xl border border-gray-800">
                        <span className="font-bold text-white block mb-1">2026</span>
                        <p className="text-sm">VitalQ founded with mission to bring quantum sensing to clinical care</p>
                    </div>
                </div>
                <div className="relative flex items-center justify-between md:justify-normal md:odd:flex-row-reverse group is-active">
                    <div className="flex items-center justify-center w-10 h-10 rounded-full border border-gray-700 bg-zinc-900 text-caladan-green shrink-0 md:order-1 md:group-odd:-translate-x-1/2 md:group-even:translate-x-1/2 shadow-sm z-10">
                        <div className="w-2 h-2 bg-caladan-green rounded-full"></div>
                    </div>
                    <div className="w-[calc(100%-4rem)] md:w-[calc(50%-2.5rem)] bg-zinc-900 p-4 rounded-xl border border-gray-800 text-right md:text-left md:group-even:text-right">
                        <span className="font-bold text-white block mb-1">Current</span>
                        <p className="text-sm">Developing quantum-enhanced prototype and establishing foundry partnerships for integrated photonic chip fabrication</p>
                    </div>
                </div>
                <div className="relative flex items-center justify-between md:justify-normal md:odd:flex-row-reverse group is-active">
                    <div className="flex items-center justify-center w-10 h-10 rounded-full border border-gray-700 bg-zinc-900 text-gray-500 shrink-0 md:order-1 md:group-odd:-translate-x-1/2 md:group-even:translate-x-1/2 shadow-sm z-10"></div>
                    <div className="w-[calc(100%-4rem)] md:w-[calc(50%-2.5rem)] bg-zinc-900/50 p-4 rounded-xl border border-gray-800">
                        <span className="font-bold text-white block mb-1">2026-2027</span>
                        <p className="text-sm">Classical optical prototype validation, tissue phantom testing, initial funding secured</p>
                    </div>
                </div>
                <div className="relative flex items-center justify-between md:justify-normal md:odd:flex-row-reverse group is-active">
                    <div className="flex items-center justify-center w-10 h-10 rounded-full border border-gray-700 bg-zinc-900 text-gray-500 shrink-0 md:order-1 md:group-odd:-translate-x-1/2 md:group-even:translate-x-1/2 shadow-sm z-10"></div>
                    <div className="w-[calc(100%-4rem)] md:w-[calc(50%-2.5rem)] bg-zinc-900/50 p-4 rounded-xl border border-gray-800 text-right md:text-left md:group-even:text-right">
                        <span className="font-bold text-white block mb-1">2027-2028</span>
                        <p className="text-sm">Quantum enhancement integration, pre-clinical studies, clinical partnerships established</p>
                    </div>
                </div>
                <div className="relative flex items-center justify-between md:justify-normal md:odd:flex-row-reverse group is-active">
                    <div className="flex items-center justify-center w-10 h-10 rounded-full border border-gray-700 bg-zinc-900 text-gray-500 shrink-0 md:order-1 md:group-odd:-translate-x-1/2 md:group-even:translate-x-1/2 shadow-sm z-10"></div>
                    <div className="w-[calc(100%-4rem)] md:w-[calc(50%-2.5rem)] bg-zinc-900/50 p-4 rounded-xl border border-gray-800">
                        <span className="font-bold text-white block mb-1">2028-2029</span>
                        <p className="text-sm">Clinical trials, regulatory submissions (CE Mark, FDA)</p>
                    </div>
                </div>
                <div className="relative flex items-center justify-between md:justify-normal md:odd:flex-row-reverse group is-active">
                    <div className="flex items-center justify-center w-10 h-10 rounded-full border border-gray-700 bg-zinc-900 text-caladan-green shrink-0 md:order-1 md:group-odd:-translate-x-1/2 md:group-even:translate-x-1/2 shadow-sm z-10">
                    </div>
                    <div className="w-[calc(100%-4rem)] md:w-[calc(50%-2.5rem)] bg-zinc-900/50 p-4 rounded-xl border border-gray-800 text-right md:text-left md:group-even:text-right">
                        <span className="font-bold text-white block mb-1">2030+</span>
                        <p className="text-sm">Commercial launch, transforming post-operative care worldwide</p>
                    </div>
                </div>
            </div>
        </div>
      </section>

      {/* Join Us & Contact Section */}
      <section className="py-24 bg-zinc-900 border-t border-gray-800">
        <div className="max-w-5xl mx-auto px-6 lg:px-8">
            <div className="grid grid-cols-1 lg:grid-cols-2 gap-16">
                <div>
                    <h2 className="text-3xl font-bold text-white mb-6">Join Us</h2>
                    <p className="text-gray-300 mb-8 text-lg">We're at the beginning of something transformative. If you're passionate about using quantum technology to save lives, we want to hear from you.</p>
                    
                    <div className="space-y-6">
                        <div>
                            <h4 className="text-white font-semibold flex items-center gap-2"><span className="w-1.5 h-1.5 rounded-full bg-caladan-green"></span> Investors</h4>
                            <p className="text-gray-400 text-sm mt-1">We're raising our seed round to accelerate prototype development and secure key partnerships. Contact us to learn more.</p>
                        </div>
                        <div>
                            <h4 className="text-white font-semibold flex items-center gap-2"><span className="w-1.5 h-1.5 rounded-full bg-caladan-green"></span> Clinical Partners</h4>
                            <p className="text-gray-400 text-sm mt-1">We're seeking hospital partners for future clinical validation. If you're interested in early sepsis detection technology, let's talk.</p>
                        </div>
                        <div>
                            <h4 className="text-white font-semibold flex items-center gap-2"><span className="w-1.5 h-1.5 rounded-full bg-caladan-green"></span> Talent</h4>
                            <p className="text-gray-400 text-sm mt-1">We're building our founding team. If you're an expert in quantum optics, biomedical engineering, clinical medicine, or medical device development, explore opportunities with us.</p>
                        </div>
                        <div>
                            <h4 className="text-white font-semibold flex items-center gap-2"><span className="w-1.5 h-1.5 rounded-full bg-caladan-green"></span> Collaborators</h4>
                            <p className="text-gray-400 text-sm mt-1">We welcome partnerships with quantum photonics research groups, medical device manufacturers, and healthcare innovators.</p>
                        </div>
                    </div>
                </div>
                
                <div className="bg-caladan-dark p-8 md:p-10 rounded-3xl border border-gray-800">
                    <h2 className="text-3xl font-bold text-white mb-8">Contact</h2>
                    <div className="space-y-6 text-gray-300">
                        <div className="flex items-start gap-4">
                            <div className="mt-1 w-10 h-10 rounded-full bg-zinc-900 border border-gray-700 flex items-center justify-center shrink-0">
                                ✉️
                            </div>
                            <div>
                                <p className="text-sm text-gray-500 font-medium uppercase tracking-wider mb-1">Email</p>
                                <a href="mailto:contact@vitalq.com" className="text-white hover:text-caladan-green transition-colors text-lg">contact@vitalq.com</a>
                            </div>
                        </div>
                        <div className="flex items-start gap-4">
                            <div className="mt-1 w-10 h-10 rounded-full bg-zinc-900 border border-gray-700 flex items-center justify-center shrink-0">
                                🔗
                            </div>
                            <div>
                                <p className="text-sm text-gray-500 font-medium uppercase tracking-wider mb-1">LinkedIn</p>
                                <a href="#" className="text-white hover:text-caladan-green transition-colors text-lg">VitalQ Company Page</a>
                            </div>
                        </div>
                        <div className="flex items-start gap-4">
                            <div className="mt-1 w-10 h-10 rounded-full bg-zinc-900 border border-gray-700 flex items-center justify-center shrink-0">
                                📍
                            </div>
                            <div>
                                <p className="text-sm text-gray-500 font-medium uppercase tracking-wider mb-1">Location</p>
                                <p className="text-white text-lg">London, UK / San Francisco, CA</p>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
      </section>
    </>
  );
}
