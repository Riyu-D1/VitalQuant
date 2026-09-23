import React, { useState } from 'react';
import { Activity, AlertTriangle, Clock, HeartPulse, ShieldCheck, Zap } from 'lucide-react';
import SplitText from '../components/ui/SplitText';
import FadeContent from '../components/ui/FadeContent';

export default function TryIt() {
  const [hours, setHours] = useState(0);

  // Simulation Constants
  const MAX_HOURS = 24;
  const QUANTUM_ALERT_TIME = 10.5;
  const CLASSICAL_ALERT_TIME = 14.5;
  const CLINICAL_SYMPTOM_TIME = 18.0;

  // Simulated Biomarker Readings
  const il6 = 10 + (200 - 10) / (1 + Math.exp(-(hours - 12) / 3));
  const lactate = 1.0 + (5.0 - 1.0) / (1 + Math.exp(-(hours - 14) / 2));
  const temp = 36.5 + 1.5 / (1 + Math.exp(-(hours - 13) / 2));
  const aggregationFraction = Math.max(0, Math.min(1, hours / 24)); // roughly 0 to 1

  const quantumAlert = hours >= QUANTUM_ALERT_TIME;
  const classicalAlert = hours >= CLASSICAL_ALERT_TIME;
  const symptomatic = hours >= CLINICAL_SYMPTOM_TIME;

  // Calculate advantage and stats
  const hoursSaved = hours >= QUANTUM_ALERT_TIME ? Math.max(0, CLASSICAL_ALERT_TIME - hours) : 0;
  const totalAdvantage = CLASSICAL_ALERT_TIME - QUANTUM_ALERT_TIME;
  const survivalBoost = (hoursSaved * 7.6).toFixed(1);

  return (
    <div className="pt-32 pb-20 px-6 lg:px-8 max-w-7xl mx-auto flex flex-col gap-10">
      <div className="text-center">
        <FadeContent>
          <span className="eyebrow">
            <span className="eyebrow-dot" />
            Interactive demo
          </span>
        </FadeContent>
        <h1 className="font-rx100 mt-4">
          <SplitText
            text="Simulation Dashboard"
            className="block text-4xl md:text-6xl text-white tracking-tight"
            splitType="chars"
            delay={40}
            duration={0.9}
            textAlign="center"
          />
        </h1>
        <FadeContent delay={500} duration={900}>
          <p className="mt-5 text-xl text-gray-400 max-w-3xl mx-auto">
            Experience the quantum advantage. Adjust the timeline to see how our
            quantum-enhanced detection identifies faint biomarker signals earlier
            than classical methods.
          </p>
        </FadeContent>
      </div>

      {/* Main Control Panel */}
      <FadeContent delay={200} duration={900}>
        <div className="bg-zinc-950/80 border border-zinc-800 rounded-2xl p-8 backdrop-blur-sm card-lift">
          <h2 className="text-2xl font-rx100 text-white mb-6 flex items-center gap-2">
            <Clock className="text-caladan-green" /> Timeline: {hours.toFixed(1)} Hours Post-Surgery
          </h2>

          <input
            type="range"
            min="0"
            max={MAX_HOURS}
            step="0.5"
            value={hours}
            onChange={(e) => setHours(parseFloat(e.target.value))}
            className="w-full h-3 bg-zinc-800 rounded-lg appearance-none cursor-pointer accent-caladan-green outline-none mb-8"
          />

          <div className="relative w-full h-8 mb-4 hidden md:block text-xs font-semibold text-gray-500">
            <div className="absolute left-[43.7%] transform -translate-x-1/2 flex flex-col items-center">
              <div className="h-4 w-0.5 bg-caladan-green mb-1"></div>
              Quantum Alert (10.5h)
            </div>
            <div className="absolute left-[60.4%] transform -translate-x-1/2 flex flex-col items-center">
              <div className="h-4 w-0.5 bg-red-500 mb-1"></div>
              Classical Alert (14.5h)
            </div>
            <div className="absolute left-[75%] transform -translate-x-1/2 flex flex-col items-center">
              <div className="h-4 w-0.5 bg-gray-400 mb-1"></div>
              Clinical Symptoms (18h)
            </div>
          </div>
        </div>
      </FadeContent>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
        {/* Real-time Biomarker Display */}
        <FadeContent delay={300} duration={900}>
          <div className="bg-zinc-950/80 border border-zinc-800 rounded-2xl p-8 backdrop-blur-sm flex flex-col h-full">
            <h3 className="text-xl font-rx100 text-white mb-6 flex items-center gap-2">
              <Activity className="text-caladan-green" /> Patient Vitals & Biomarkers
            </h3>
            <div className="grid grid-cols-2 gap-4 flex-grow">
              <div className="bg-zinc-900/60 rounded-xl p-5 border border-zinc-800 flex flex-col justify-center">
                <span className="text-sm text-gray-400 mb-1 uppercase tracking-wider">IL-6 Levels</span>
                <span className={`text-3xl font-bold ${il6 > 40 ? 'text-orange-400' : 'text-white'}`}>
                  {il6.toFixed(1)} <span className="text-sm font-normal text-gray-500">pg/mL</span>
                </span>
              </div>
              <div className="bg-zinc-900/60 rounded-xl p-5 border border-zinc-800 flex flex-col justify-center">
                <span className="text-sm text-gray-400 mb-1 uppercase tracking-wider">Lactate</span>
                <span className={`text-3xl font-bold ${lactate > 2.5 ? 'text-orange-400' : 'text-white'}`}>
                  {lactate.toFixed(2)} <span className="text-sm font-normal text-gray-500">mmol/L</span>
                </span>
              </div>
              <div className="bg-zinc-900/60 rounded-xl p-5 border border-zinc-800 flex flex-col justify-center">
                <span className="text-sm text-gray-400 mb-1 uppercase tracking-wider">Temperature</span>
                <span className={`text-3xl font-bold ${temp > 37.5 ? 'text-orange-400' : 'text-white'}`}>
                  {temp.toFixed(1)} <span className="text-sm font-normal text-gray-500">°C</span>
                </span>
              </div>
              <div className="bg-zinc-900/60 rounded-xl p-5 border border-zinc-800 flex flex-col justify-center">
                <span className="text-sm text-gray-400 mb-1 uppercase tracking-wider">Aggreg. Fraction</span>
                <span className="text-3xl font-bold text-purple-400">
                  {(aggregationFraction * 100).toFixed(0)} <span className="text-sm font-normal text-gray-500">%</span>
                </span>
              </div>
            </div>
          </div>
        </FadeContent>

        {/* System Comparison */}
        <FadeContent delay={400} duration={900}>
          <div className="flex flex-col gap-6">
            {/* Classical Detection */}
            <div className={`rounded-2xl p-6 border transition-all duration-500 flex items-center justify-between
              ${classicalAlert
                  ? 'bg-red-950/40 border-red-500/50'
                  : 'bg-zinc-950/80 border-zinc-800'}`}
            >
              <div>
                <h3 className="text-xl font-rx100 text-white mb-2">Classical System</h3>
                <p className="text-gray-400 text-sm">Standard near-infrared spectroscopy</p>
              </div>
              <div className="text-right flex items-center gap-3">
                {classicalAlert ? (
                  <>
                    <span className="text-red-400 font-bold text-lg animate-pulse uppercase">Sepsis Detected</span>
                    <AlertTriangle className="text-red-500 w-8 h-8" />
                  </>
                ) : (
                  <>
                    <span className="text-gray-500 font-bold text-lg uppercase">Monitoring...</span>
                    <ShieldCheck className="text-gray-600 w-8 h-8" />
                  </>
                )}
              </div>
            </div>

            {/* Quantum Detection */}
            <div className={`rounded-2xl p-6 border transition-all duration-500 flex items-center justify-between
              ${quantumAlert
                  ? 'bg-caladan-green/10 border-caladan-green/50 shadow-[0_0_40px_rgba(20,200,113,0.15)]'
                  : 'bg-zinc-950/80 border-zinc-800'}`}
            >
              <div>
                <h3 className="text-xl font-rx100 text-white mb-2 flex items-center gap-2">Quantum System <Zap className="w-5 h-5 text-caladan-green" /></h3>
                <p className="text-gray-400 text-sm">Squeezed-light enhanced detection</p>
              </div>
              <div className="text-right flex items-center gap-3">
                {quantumAlert ? (
                  <>
                    <span className="text-caladan-green font-bold text-lg animate-pulse uppercase">Sepsis Detected</span>
                    <AlertTriangle className="text-caladan-green w-8 h-8" />
                  </>
                ) : (
                  <>
                    <span className="text-gray-500 font-bold text-lg uppercase">Monitoring...</span>
                    <ShieldCheck className="text-gray-600 w-8 h-8" />
                  </>
                )}
              </div>
            </div>

            {/* Metrics */}
            <div className="bg-gradient-to-br from-zinc-900 to-zinc-950 border border-zinc-800 rounded-2xl p-6 mt-2 relative overflow-hidden">
              <div className="absolute top-0 right-0 p-4 opacity-5 pointer-events-none">
                <HeartPulse className="w-32 h-32" />
              </div>
              <h3 className="text-lg font-rx100 text-white mb-4 uppercase tracking-wider text-center">Clinical Outcomes</h3>
              <div className="grid grid-cols-2 md:grid-cols-3 gap-4">
                <div className="text-center">
                  <div className="text-4xl font-extrabold text-blue-400 mb-1 pt-2">
                    {hoursSaved.toFixed(1)} <span className="text-xl font-medium text-gray-500">hrs</span>
                  </div>
                  <div className="text-sm text-gray-400">Earlier Detection</div>
                </div>
                <div className="text-center border-l border-zinc-800">
                  <div className="text-4xl font-extrabold text-caladan-green mb-1 pt-2">
                    +{survivalBoost} <span className="text-xl font-medium text-gray-500">%</span>
                  </div>
                  <div className="text-sm text-gray-400">Survival Boost</div>
                </div>
                <div className="text-center border-t md:border-t-0 md:border-l border-zinc-800 col-span-2 md:col-span-1 pt-4 md:pt-0">
                  <div className="text-4xl font-extrabold text-purple-400 mb-1 md:pt-2">
                    {Math.round((hoursSaved * 7.6 / 100) * 270000).toLocaleString()}
                  </div>
                  <div className="text-sm text-gray-400">Est. Lives Saved / Year</div>
                </div>
              </div>

              {hours >= CLINICAL_SYMPTOM_TIME && !classicalAlert && (
                <div className="mt-6 text-center text-sm font-semibold text-red-400 bg-red-950/40 py-2 rounded-lg">
                  Patient is showing symptoms. Treatment starts late.
                </div>
              )}
              {hours >= CLINICAL_SYMPTOM_TIME && classicalAlert && (
                <div className="mt-6 text-center text-sm font-semibold text-orange-400 bg-orange-950/40 py-2 rounded-lg">
                  Late stage intervention limits effectiveness.
                </div>
              )}
              {quantumAlert && hours < CLASSICAL_ALERT_TIME && (
                <div className="mt-6 text-center text-sm font-semibold text-caladan-green bg-green-950/30 py-2 rounded-lg border border-green-900/50">
                  Early intervention window open! Administering antibiotics.
                </div>
              )}
            </div>
          </div>
        </FadeContent>
      </div>
    </div>
  );
}
