import { Link } from 'react-router-dom';
import { GoArrowRight } from 'react-icons/go';
import { Icon } from '@chakra-ui/react';
import { Sparkles } from 'lucide-react';

const Hero = () => {
  return (
    <div className="landing-content">
      {/* Single blur element - using CSS gradient instead of SVG filter for performance */}
      <div className="landing-gradient-blur" aria-hidden="true" />

      <div className="hero-main-content">
        <div className="hero-tag-fade">
          <Link to="/components/border-glow" className="hero-new-badge-container">
            <span className="hero-new-badge">
              New <Icon boxSize={4} as={Sparkles} />
            </span>
            <div className="hero-new-badge-text">
              <span>Border Glow</span>
              <GoArrowRight />
            </div>
          </Link>
        </div>

        <h1 className="landing-title">
          <span className="hero-text-animate">React Components</span>
          <br />
          <span className="hero-text-animate hero-text-animate-delay">For Creative Developers</span>
        </h1>

        <p className="landing-subtitle hero-text-animate hero-subtitle-delay">
          Highly customizable animated components that make your React projects truly stand out
        </p>

        <div className="hero-text-animate hero-button-delay">
          <Link to={'/get-started/index'} className="landing-button">
            <span>Browse Components</span>
            <div className="button-arrow-circle">
              <svg width="16" height="16" viewBox="0 0 16 16" fill="#ffffff" xmlns="http://www.w3.org/2000/svg">
                <path
                  d="M6 12L10 8L6 4"
                  stroke="#4c1d95"
                  strokeWidth="2"
                  strokeLinecap="round"
                  strokeLinejoin="round"
                />
              </svg>
            </div>
          </Link>
        </div>
      </div>
    </div>
  );
};

export default Hero;
