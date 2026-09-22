import React, { useState, useEffect } from 'react';
import { Link, useLocation } from 'react-router-dom';
import { Menu, X } from 'lucide-react';
import Logo from './Logo';

export default function Navbar() {
  const [isOpen, setIsOpen] = useState(false);
  const location = useLocation();

  // Close mobile menu when route changes
  useEffect(() => {
    setIsOpen(false);
  }, [location]);

  // Prevent background scrolling when menu is open
  useEffect(() => {
    if (isOpen) {
      document.body.style.overflow = 'hidden';
    } else {
      document.body.style.overflow = 'unset';
    }
    return () => {
      document.body.style.overflow = 'unset';
    };
  }, [isOpen]);

  return (
    <>
      <nav className="fixed w-full z-50 bg-caladan-dark/90 backdrop-blur-md border-b border-gray-800 transition-all duration-300">
          <div className="max-w-7xl mx-auto px-6 lg:px-8">
              <div className="flex justify-between items-center h-24">
                  <div className="flex-shrink-0 flex items-center">
                      <Link to="/" className="font-bold text-xl text-white tracking-tight flex items-center gap-3" onClick={() => setIsOpen(false)}>
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

                  {/* Mobile Menu Toggle */}
                  <div className="md:hidden flex items-center">
                      <button
                          onClick={() => setIsOpen(!isOpen)}
                          className="text-white hover:text-caladan-green transition-colors p-2 -mr-2"
                          aria-label="Toggle menu"
                      >
                          {isOpen ? <X size={28} /> : <Menu size={28} />}
                      </button>
                  </div>
              </div>
          </div>
      </nav>

      {/* Mobile Menu Overlay */}
      <div
          className={`fixed inset-0 bg-black/60 backdrop-blur-sm z-40 transition-opacity duration-300 md:hidden ${
              isOpen ? 'opacity-100' : 'opacity-0 pointer-events-none'
          }`}
          onClick={() => setIsOpen(false)}
      />

      {/* Mobile Menu Sidebar */}
      <div
          className={`fixed top-0 right-0 h-full w-4/5 max-w-sm bg-caladan-dark border-l border-gray-800 z-50 transform transition-transform duration-300 ease-in-out md:hidden flex flex-col ${
              isOpen ? 'translate-x-0' : 'translate-x-full'
          }`}
      >
          <div className="flex justify-between items-center px-6 h-24 border-b border-gray-800/50">
              <span className="font-bold text-white uppercase tracking-widest text-sm">Menu</span>
              <button
                  onClick={() => setIsOpen(false)}
                  className="text-gray-400 hover:text-white transition-colors p-2 -mr-2"
              >
                  <X size={28} />
              </button>
          </div>
          
          <div className="flex flex-col px-8 py-8 space-y-8 overflow-y-auto">
              <Link to="/#product" className="text-xl font-medium text-white hover:text-caladan-green transition-colors uppercase tracking-wider" onClick={() => setIsOpen(false)}>
                  Product
              </Link>
              <Link to="/about" className="text-xl font-medium text-white hover:text-caladan-green transition-colors uppercase tracking-wider" onClick={() => setIsOpen(false)}>
                  About
              </Link>
              <Link to="/tryit" className="text-xl font-medium text-caladan-green hover:text-green-400 transition-colors uppercase tracking-wider" onClick={() => setIsOpen(false)}>
                  Try It
              </Link>
              <Link to="/contact" className="text-xl font-medium text-white hover:text-caladan-green transition-colors uppercase tracking-wider" onClick={() => setIsOpen(false)}>
                  Contact
              </Link>
          </div>
      </div>
    </>
  );
}
