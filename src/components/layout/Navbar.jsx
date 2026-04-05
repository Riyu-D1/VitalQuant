import React from 'react';
import { Link } from 'react-router-dom';
import Logo from './Logo';

export default function Navbar() {
  return (
    <nav className="fixed w-full z-50 bg-caladan-dark/90 backdrop-blur-md border-b border-gray-800 transition-all duration-300">
        <div className="max-w-7xl mx-auto px-6 lg:px-8">
            <div className="flex justify-between items-center h-24">
                <div className="flex-shrink-0 flex items-center">
                    <Link to="/" className="font-bold text-xl text-white tracking-tight flex items-center gap-3">
                        <Logo className="w-12 h-12" />
                        <span className="text-2xl pt-1">VITALQ</span>
                    </Link>
                </div>
                {/* Desktop Menu */}
                <div className="hidden md:flex space-x-10 items-center">
                    <Link to="/#product" className="text-sm font-semibold hover:text-caladan-green transition-colors uppercase tracking-widest">Product</Link>
                    <Link to="/about" className="text-sm font-semibold hover:text-caladan-green transition-colors uppercase tracking-widest">About</Link>
                    <Link to="/tryit" className="text-sm font-semibold hover:text-caladan-green transition-colors uppercase tracking-widest text-caladan-green">Try It</Link>
                    <Link to="/contact" className="text-sm font-semibold hover:text-caladan-green transition-colors uppercase tracking-widest">Contact</Link>
                </div>
            </div>
        </div>
    </nav>
  );
}
