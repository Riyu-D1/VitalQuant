import React from 'react';

export default function Contact() {
  return (
    <div className="pt-32 pb-24 px-6 lg:px-8 max-w-7xl mx-auto min-h-screen">
      <div className="max-w-3xl mx-auto">
        <h1 className="text-4xl md:text-5xl font-bold text-white mb-6 tracking-tight">Contact Us</h1>
        <p className="text-gray-400 text-lg mb-12">
          Interested in bringing Quantum Photonics to your organization? Schedule a meeting with our team or drop us a line below.
        </p>
        
        <div className="bg-zinc-900 border border-gray-800 rounded-2xl p-8 mb-12">
          <h2 className="text-2xl font-bold text-white mb-6">Schedule a Meeting</h2>
          <p className="text-gray-400 mb-8">
            Book directly on our calendar to discuss a pilot, partnership, or customized deployment.
          </p>
          <a
            href="#"
            className="inline-block bg-caladan-green text-caladan-dark px-8 py-4 rounded-full font-bold tracking-wide transition-all uppercase text-sm"
          >
            Book a Time slot
          </a>
        </div>

        <form className="space-y-6 bg-zinc-900 border border-gray-800 rounded-2xl p-8">
          <h2 className="text-2xl font-bold text-white mb-6">Send a Message</h2>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            <div>
              <label htmlFor="firstName" className="block text-sm font-medium text-gray-400 mb-2">First Name</label>
              <input type="text" id="firstName" className="w-full bg-caladan-dark border border-gray-800 rounded-lg px-4 py-3 text-white focus:outline-none focus:border-caladan-green focus:ring-1 focus:ring-caladan-green transition-colors" placeholder="John" />
            </div>
            <div>
              <label htmlFor="lastName" className="block text-sm font-medium text-gray-400 mb-2">Last Name</label>
              <input type="text" id="lastName" className="w-full bg-caladan-dark border border-gray-800 rounded-lg px-4 py-3 text-white focus:outline-none focus:border-caladan-green focus:ring-1 focus:ring-caladan-green transition-colors" placeholder="Doe" />
            </div>
          </div>
          <div>
            <label htmlFor="email" className="block text-sm font-medium text-gray-400 mb-2">Email Address</label>
            <input type="email" id="email" className="w-full bg-caladan-dark border border-gray-800 rounded-lg px-4 py-3 text-white focus:outline-none focus:border-caladan-green focus:ring-1 focus:ring-caladan-green transition-colors" placeholder="john@example.com" />
          </div>
          <div>
            <label htmlFor="message" className="block text-sm font-medium text-gray-400 mb-2">Message</label>
            <textarea id="message" rows="4" className="w-full bg-caladan-dark border border-gray-800 rounded-lg px-4 py-3 text-white focus:outline-none focus:border-caladan-green focus:ring-1 focus:ring-caladan-green transition-colors" placeholder="How can we help?"></textarea>
          </div>
          <button type="submit" className="w-full bg-white text-caladan-dark px-8 py-4 rounded-full font-bold tracking-wide hover:bg-gray-200 transition-colors uppercase text-sm">
            Send Message
          </button>
        </form>
      </div>
    </div>
  );
}