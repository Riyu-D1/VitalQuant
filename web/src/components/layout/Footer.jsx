import React from 'react';
import { Link } from 'react-router-dom';

export default function Footer() {
  return (
    <footer className="bg-zinc-900 pt-16 pb-8 border-t border-gray-800 mt-auto">
        <div className="max-w-7xl mx-auto px-6 lg:px-8">
            <div className="grid grid-cols-1 md:grid-cols-4 gap-12 mb-16">
                <div className="col-span-1 md:col-span-2">
                    <Link to="/" className="font-bold text-xl text-white tracking-tight mb-4 inline-block">VitalQ.</Link>
                    <p className="text-gray-300 text-sm mb-4">Next-Gen Medical Sensors</p>
                    <p className="text-gray-300 text-sm">Based in London</p>
                    <Link to="/contact" className="text-caladan-green font-semibold text-sm hover:underline mt-4 inline-block uppercase tracking-wider">Contact</Link>
                </div>
                
                <div>
                    <h4 className="font-bold text-white mb-6 uppercase tracking-wider text-sm">Links</h4>
                    <ul className="space-y-4 text-sm font-medium">
                        <li><Link to="/" className="text-gray-300 hover:text-white transition-colors">Home</Link></li>
                        <li><Link to="/about" className="text-gray-300 hover:text-white transition-colors">About</Link></li>
                    </ul>
                </div>
                
                <div>
                    <h4 className="font-bold text-white mb-6 uppercase tracking-wider text-sm">Legal & Social</h4>
                    <ul className="space-y-4 text-sm font-medium">
                        <li><a href="#" className="text-gray-300 hover:text-white transition-colors">Privacy Policy</a></li>
                        <li><a href="#" className="text-gray-300 hover:text-white transition-colors">LinkedIn</a></li>
                    </ul>
                </div>
            </div>
        </div>
    </footer>
  );
}
