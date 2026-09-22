import { useEffect, useRef } from 'react';
import { Link } from 'react-router-dom';
import { gsap } from 'gsap';
import { Logo } from '../../common/SVGComponents';
import { useStars } from '../../../hooks/useStars';
import star from '../../../assets/common/star.svg';
import './DisplayHeader.css';

const DisplayHeader = ({ activeItem }) => {
  const navRef = useRef(null);
  const starCountRef = useRef(null);
  const stars = useStars();

  useEffect(() => {
    if (stars && starCountRef.current) {
      gsap.fromTo(
        starCountRef.current,
        {
          scale: 0,
          width: 0,
          opacity: 0
        },
        {
          scale: 1,
          width: '100px',
          opacity: 1,
          duration: 0.8,
          ease: 'back.out(1)'
        }
      );
    }
  }, [stars]);

  return (
    <header className="header">
      <div className="header-container">
        <Link to="/" className="logo">
          <Logo />
        </Link>

        <div className="nav-cta-group">
          <nav className="landing-nav-items" ref={navRef}>
            <Link className={`nav-link ${activeItem === 'home' && 'active-link'}`} to="/">
              Home
            </Link>
            <Link className="nav-link" to="/get-started/introduction">
              Docs
            </Link>
            <Link className={`nav-link ${activeItem === 'showcase' && 'active-link'}`} to="/showcase">
              Showcase
            </Link>
            <Link className={`nav-link ${activeItem === 'tools' && 'active-link'}`} to="/tools">
              Tools
            </Link>
          </nav>

          <button
            className="cta-button"
            onClick={() => window.open('https://github.com/DavidHDev/react-bits', '_blank')}
          >
            Star On GitHub
            <span ref={starCountRef} style={{ opacity: 0 }}>
              <img src={star} alt="Star Icon" />
              {stars}
            </span>
          </button>
        </div>
      </div>
    </header>
  );
};

export default DisplayHeader;
