import React from 'react';
import { BrowserRouter as Router, Routes, Route } from 'react-router-dom';
import Navbar from './components/layout/Navbar';
import Footer from './components/layout/Footer';
import Home from './pages/Home';
import About from './pages/About';
import TryIt from './pages/TryIt';

export default function App() {
  return (
    <Router>
      <div className="font-sans antialiased overflow-x-hidden text-caladan-gray bg-caladan-dark min-h-screen flex flex-col">
        <Navbar />
        
        <main className="flex-grow">
          <Routes>
            <Route path="/" element={<Home />} />
            <Route path="/about" element={<About />} />
            <Route path="/tryit" element={<TryIt />} />
          </Routes>
        </main>

        <Footer />
      </div>
    </Router>
  );
}