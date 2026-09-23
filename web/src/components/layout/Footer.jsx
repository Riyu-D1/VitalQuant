import React from 'react';
import { Link } from 'react-router-dom';
import Logo from './Logo';

export default function Footer() {
  return (
    <footer className="bg-zinc-950 pt-16 pb-8 border-t border-zinc-800/60 mt-auto">
      <div className="max-w-7xl mx-auto px-6 lg:px-8">
        <div className="grid grid-cols-1 md:grid-cols-4 gap-12 mb-16">
          <div className="col-span-1 md:col-span-2">
            <Link to="/" className="flex items-center gap-3 mb-4">
              <Logo className="w-9 h-9" />
              <span className="font-rx100 text-xl text-white tracking-wide">VITALQ</span>
            </Link>
            <p className="text-gray-400 text-sm mb-1">Quantum-enhanced biosensing.</p>
            <p className="text-gray-500 text-sm">London, UK</p>
            <Link
              to="/contact"
              className="text-caladan-green font-semibold text-xs hover:text-white transition-colors mt-4 inline-block uppercase tracking-[0.2em]"
            >
              Contact
            </Link>
          </div>

          <div>
            <h4 className="font-semibold text-white mb-6 uppercase tracking-[0.2em] text-xs">Site</h4>
            <ul className="space-y-4 text-sm font-medium">
              <li><Link to="/" className="text-gray-400 hover:text-white transition-colors">Home</Link></li>
              <li><Link to="/about" className="text-gray-400 hover:text-white transition-colors">About</Link></li>
              <li><Link to="/tryit" className="text-gray-400 hover:text-white transition-colors">Try It</Link></li>
              <li><Link to="/contact" className="text-gray-400 hover:text-white transition-colors">Contact</Link></li>
            </ul>
          </div>

          <div>
            <h4 className="font-semibold text-white mb-6 uppercase tracking-[0.2em] text-xs">Legal & Social</h4>
            <ul className="space-y-4 text-sm font-medium">
              <li><a href="#" className="text-gray-400 hover:text-white transition-colors">Privacy Policy</a></li>
              <li><a href="#" className="text-gray-400 hover:text-white transition-colors">LinkedIn</a></li>
            </ul>
          </div>
        </div>

        <div className="pt-6 border-t border-zinc-800/60 flex flex-col sm:flex-row justify-between items-center gap-2">
          <p className="text-xs text-gray-600">© {new Date().getFullYear()} VitalQ. All rights reserved.</p>
          <p className="text-xs text-gray-600">Detecting sepsis before symptoms appear.</p>
        </div>
      </div>
    </footer>
  );
}
