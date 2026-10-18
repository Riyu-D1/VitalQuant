import React, { useState, useEffect } from 'react';
import { Link, useLocation } from 'react-router-dom';
import { Menu, X } from 'lucide-react';
import Logo from './Logo';

const LINKS = [
  { to: '/#product', label: 'Product' },
  { to: '/about', label: 'About', path: '/about' },
  { to: '/tryit', label: 'Try It', path: '/tryit', accent: true },
  { to: '/contact', label: 'Contact', path: '/contact' },
];

export default function Navbar() {
  const [isOpen, setIsOpen] = useState(false);
  const [scrolled, setScrolled] = useState(false);
  const location = useLocation();

  // Close mobile menu when route changes
  useEffect(() => {
    setIsOpen(false);
  }, [location]);

  useEffect(() => {
    const onScroll = () => setScrolled(window.scrollY > 12);
    onScroll();
    window.addEventListener('scroll', onScroll, { passive: true });
    return () => window.removeEventListener('scroll', onScroll);
  }, []);

  // Prevent background scrolling when menu is open
  useEffect(() => {
    document.body.style.overflow = isOpen ? 'hidden' : 'unset';
    return () => {
      document.body.style.overflow = 'unset';
    };
  }, [isOpen]);

  const linkCls = (l) =>
    `relative text-xs font-semibold uppercase tracking-[0.2em] transition-colors duration-300 ${
      l.path && location.pathname === l.path
        ? 'text-caladan-green'
        : l.accent
          ? 'text-caladan-green hover:text-white'
          : 'text-gray-300 hover:text-caladan-green'
    }`;

  return (
    <>
      <nav
        className={`fixed w-full z-50 transition-all duration-500 border-b ${
          scrolled
            ? 'bg-caladan-dark/90 backdrop-blur-md border-zinc-800/80'
            : 'bg-transparent backdrop-blur-sm border-transparent'
        }`}
      >
        <div className="max-w-7xl mx-auto px-6 lg:px-8">
          <div
            className={`flex justify-between items-center transition-all duration-500 ${
              scrolled ? 'h-16' : 'h-24'
            }`}
          >
            <div className="flex-shrink-0 flex items-center">
              <Link
                to="/"
                className="font-bold text-xl text-white tracking-tight flex items-center gap-3"
                onClick={() => setIsOpen(false)}
              >
                <Logo className={scrolled ? 'w-9 h-9' : 'w-11 h-11'} />
                <span className="font-rx100 text-xl pt-0.5 tracking-wide">VITALQ</span>
              </Link>
            </div>

            {/* Desktop Menu */}
            <div className="hidden md:flex space-x-10 items-center">
              {LINKS.map((l) => (
                <Link key={l.label} to={l.to} className={linkCls(l)}>
                  {l.label}
                  {l.path && location.pathname === l.path && (
                    <span className="absolute -bottom-2 left-0 right-0 h-px bg-caladan-green" />
                  )}
                </Link>
              ))}
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
        className={`fixed top-0 right-0 h-full w-4/5 max-w-sm bg-caladan-dark border-l border-zinc-800 z-50 transform transition-transform duration-300 ease-in-out md:hidden flex flex-col ${
          isOpen ? 'translate-x-0' : 'translate-x-full'
        }`}
      >
        <div className="flex justify-between items-center px-6 h-24 border-b border-zinc-800/50">
          <span className="font-bold text-white uppercase tracking-widest text-sm">Menu</span>
          <button
            onClick={() => setIsOpen(false)}
            className="text-gray-400 hover:text-white transition-colors p-2 -mr-2"
          >
            <X size={28} />
          </button>
        </div>

        <div className="flex flex-col px-8 py-8 space-y-8 overflow-y-auto">
          {LINKS.map((l) => (
            <Link
              key={l.label}
              to={l.to}
              className={`text-xl font-medium uppercase tracking-wider transition-colors ${
                l.accent ? 'text-caladan-green' : 'text-white hover:text-caladan-green'
              }`}
              onClick={() => setIsOpen(false)}
            >
              {l.label}
            </Link>
          ))}
        </div>
      </div>
    </>
  );
}
