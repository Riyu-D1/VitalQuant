import React from 'react';
import { Link } from 'react-router-dom';
import Dither from '../components/ui/Dither';

export default function Home() {
  return (
    <>
      <section className="relative pt-48 pb-20 lg:pt-56 lg:pb-32 overflow-hidden bg-caladan-dark">
          <div className="absolute inset-0 z-0">
             <Dither
                waveColor={[0.0392156862745098, 0.6196078431372549, 0.34901960784313724]}
                disableAnimation={false}
                enableMouseInteraction={true}
                mouseRadius={0.3}
                colorNum={4}
                pixelSize={2}
                waveAmplitude={0.3}
                waveFrequency={3}
                waveSpeed={0.05}
              />
          </div>
          <div className="absolute inset-x-0 bottom-0 h-48 bg-gradient-to-t from-caladan-dark to-transparent z-0 pointer-events-none"></div>
          
          <div className="max-w-7xl mx-auto px-6 lg:px-8 relative z-10 pointer-events-none">
              <div className="max-w-4xl fade-in-up pointer-events-auto">
                  <h1 className="hero-title font-rx100 text-5xl sm:text-6xl lg:text-[5.5rem] font-bold text-white mb-8 leading-tight tracking-[-0.04em]">
                      Detecting sepsis before <br />
                      <span className="text-caladan-green">symptoms appear.</span>
                  </h1>
                  <p className="text-lg sm:text-xl text-gray-300 font-normal mb-12 max-w-2xl leading-relaxed">
                      Earlier detection. Better outcomes. Saved lives. Vitalquant combines unprecedented sensor hardware, intuitive predictive software, and continuous monitoring into one streamlined wearable.
                  </p>
                  <div className="flex flex-col sm:flex-row gap-4">
                      <a href="#product" className="bg-caladan-green hover:bg-green-500 text-white px-8 py-4 rounded-full font-bold tracking-wide transition-all text-center uppercase text-sm">
                          Learn More
                      </a>
                      <a href="#contact" className="bg-transparent hover:bg-zinc-900/50 text-white border border-gray-800 px-8 py-4 rounded-full font-bold tracking-wide transition-all text-center uppercase text-sm">
                          Schedule A Meeting
                      </a>
                  </div>
              </div>
          </div>
      </section>

      <section className="py-20 border-t border-gray-800 bg-zinc-900">
          <div className="max-w-7xl mx-auto px-6 lg:px-8">
              <h2 className="text-center text-sm font-bold text-gray-300 uppercase tracking-widest mb-12">The Critical Gap in Care</h2>
              <div className="grid grid-cols-1 md:grid-cols-3 gap-8 text-center divide-y md:divide-y-0 md:divide-x divide-gray-800">
                  <div className="p-6">
                      <div className="text-5xl font-extrabold font-rx100 text-white mb-4">310M+</div>
                      <p className="text-gray-300 font-medium">Surgeries globally each year</p>
                  </div>
                  <div className="p-6">
                      <div className="text-5xl font-extrabold font-rx100 text-white mb-4">70%</div>
                      <p className="text-gray-300 font-medium">Mortality rate from late-stage sepsis</p>
                  </div>
                  <div className="p-6">
                      <div className="text-5xl font-extrabold font-rx100 text-white mb-4">12<span className="text-2xl ml-1">hrs</span></div>
                      <p className="text-gray-300 font-medium">Too late: the lag of current detection</p>
                  </div>
              </div>
          </div>
      </section>

      <section id="product" className="py-32 bg-caladan-dark">
          <div className="max-w-7xl mx-auto px-6 lg:px-8 space-y-32">
              <div className="grid lg:grid-cols-2 gap-16 items-center">
                  <div className="order-2 lg:order-1">
                      <h2 className="section-title font-rx100 text-4xl lg:text-5xl font-bold text-white mb-6 tracking-tight leading-tight">Quantum-Enhanced Detection</h2>
                      <p className="text-lg text-gray-300 leading-relaxed mb-8 flex-col space-y-4">
                          Our sensor uses squeezed light—a quantum photonics breakthrough—to achieve sensitivity far beyond conventional monitors. By measuring inflammatory and metabolic biomarkers continuously, we detect sepsis 6-12 hours before symptoms appear, giving clinicians critical time to intervene.
                      </p>
                  </div>
                  <div className="order-1 lg:order-2 bg-zinc-900 rounded-3xl aspect-[4/3] flex items-center justify-center relative overflow-hidden border border-gray-800">
                      <div className="absolute w-[150%] h-[150%] bg-gradient-to-tr from-caladan-green/20 to-transparent rounded-full blur-3xl opacity-50"></div>
                      <img src="/image.png" alt="Quantum Sensor" className="w-full h-full object-contain relative z-10 rounded-3xl p-4" />
                  </div>
              </div>

              <div className="grid lg:grid-cols-2 gap-16 items-center">
                  <div className="bg-caladan-dark rounded-3xl aspect-[4/3] flex items-center justify-center relative overflow-hidden border border-gray-800">
                      <div className="absolute w-[150%] h-[150%] bg-gradient-to-tl from-caladan-green/20 to-transparent rounded-full blur-3xl opacity-30"></div>
                      <img src="/image copy.png" alt="AI Engine" className="w-full h-full object-contain relative z-10 rounded-3xl p-4" />
                  </div>
                  <div>
                      <h2 className="section-title font-rx100 text-4xl lg:text-5xl font-bold text-white mb-6 tracking-tight leading-tight">Pattern Recognition at Scale</h2>
                      <p className="text-lg text-gray-300 leading-relaxed mb-8">
                          Multi-parameter time-series data streams to cloud infrastructure where neural networks trained on tens of thousands of sepsis cases identify pre-symptomatic signatures. Continuous baseline tracking ensures patient-specific risk scoring with clinical-grade specificity.
                      </p>
                  </div>
              </div>

              <div className="grid lg:grid-cols-2 gap-16 items-center">
                  <div className="order-2 lg:order-1">
                      <h2 className="section-title font-rx100 text-4xl lg:text-5xl font-bold text-white mb-6 tracking-tight leading-tight">Built for Real Hospitals</h2>
                      <p className="text-lg text-gray-300 leading-relaxed mb-8">
                          We designed VitalQ for the reality of busy surgical wards. One wearable sensor. Wireless data transmission. Automated alerts to the right clinician at the right time. No extra nursing burden, no workflow disruption—just earlier detection when it saves lives.
                      </p>
                  </div>
                  <div className="order-1 lg:order-2 bg-zinc-900 rounded-3xl aspect-[4/3] flex items-center justify-center relative overflow-hidden border border-gray-800">
                       <div className="absolute w-full h-full bg-[radial-gradient(ellipse_at_center,_var(--tw-gradient-stops))] from-zinc-800 via-zinc-900 to-zinc-900"></div>
                       <img src="/image copy 2.png" alt="Alert System" className="w-full h-full object-contain relative z-10 rounded-3xl p-4" />
                  </div>
              </div>
          </div>
      </section>

      {/* People / Founders Section */}
      <section id="team" className="py-32 bg-zinc-900/50 border-t border-gray-800">
        <div className="max-w-7xl mx-auto px-6 lg:px-8">
            <div className="text-center mb-20">
                <h2 className="section-title font-rx100 text-4xl lg:text-5xl font-bold font-rx100 text-white mb-6">The minds behind the VitalQ leap.</h2>
                <p className="text-lg text-gray-300 max-w-2xl mx-auto leading-relaxed">
                    Our team merges expertise in quantum physics, biomedical engineering, and artificial intelligence to bring this vision to life.
                </p>
            </div>
            
            <div className="grid grid-cols-1 md:grid-cols-2 gap-12 max-w-4xl mx-auto">
                <div className="bg-caladan-dark rounded-3xl p-8 border border-gray-800 shadow-stone-900 hover:shadow-stone-800 transition-shadow">
                    <div className="w-24 h-24 rounded-full bg-zinc-900 mb-6 flex items-center justify-center text-gray-300 font-bold text-xl border border-gray-700">
                        RD
                    </div>
                    <h3 className="text-xl font-bold text-white mb-1">Riyansh Diwan</h3>
                    <p className="text-caladan-green text-sm font-semibold uppercase tracking-wider mb-4">Co-Founder</p>
                    <p className="text-gray-300 leading-relaxed text-sm">
                        Visionary leader bridging the gap between advanced sensors and practical clinical deployments.
                    </p>
                </div>

                <div className="bg-caladan-dark rounded-3xl p-8 border border-gray-800 shadow-stone-900 hover:shadow-stone-800 transition-shadow">
                    <div className="w-24 h-24 rounded-full bg-zinc-900 mb-6 flex items-center justify-center text-gray-300 font-bold text-xl border border-gray-700">
                        NP
                    </div>
                    <h3 className="text-xl font-bold text-white mb-1">Naga Perla</h3>
                    <p className="text-caladan-green text-sm font-semibold uppercase tracking-wider mb-4">Co-Founder</p>
                    <p className="text-gray-300 leading-relaxed text-sm">
                        Expert engineer directing the development of quantum-based patient tracking solutions.
                    </p>
                </div>
            </div>
        </div>
      </section>

      <section id="contact" className="py-32 bg-caladan-dark border-t border-gray-800">
          <div className="max-w-5xl mx-auto px-6 lg:px-8 text-center">
              <h2 className="section-title font-rx100 text-5xl font-bold text-white mb-8">Get in touch.</h2>
              <p className="text-xl text-gray-300 mb-12 leading-relaxed">
                  The Vitalquant sensor gives you proactive patient monitoring with real-time analytics, automated alerts, and unprecedented specificity—all in a user-friendly wearable. Get in touch to pilot our next-generation diagnostic device.
              </p>
              <div className="flex flex-col sm:flex-row justify-center gap-4">
                  <a href="#" className="bg-caladan-green hover:bg-green-500 text-white px-8 py-4 rounded-full font-bold tracking-wide transition-all uppercase text-sm">
                      Learn More
                  </a>
                  <a href="#" className="bg-transparent hover:bg-zinc-900/50 text-white border border-gray-800 px-8 py-4 rounded-full font-bold tracking-wide transition-all uppercase text-sm">
                      Schedule A Meeting
                  </a>
              </div>
          </div>
      </section>
    </>
  );
}
