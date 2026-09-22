import { Box, Text } from '@chakra-ui/react';
import useScrollToTop from '../hooks/useScrollToTop';

import FadeContent from '../content/Animations/FadeContent/FadeContent';
import Footer from '../components/landing/Footer/Footer';
import { SHOWCASE_ITEMS } from '../constants/Showcase';

import '../css/showcase.css';
import Aurora from '../content/Backgrounds/Aurora/Aurora';

const ShowcasePage = () => {
  useScrollToTop();

  return (
    <>
      <section className="showcase-wrapper">
        <Box position="fixed" top={0} left={0} right={0} bottom={0} zIndex={0} opacity={0.5} pointerEvents="none">
          <Aurora colorStops={['#3A0CA3', '#7209B7', '#4C1D95']} amplitude={0.5} blend={0.5} />
        </Box>
        <title>React Bits - Showcase 🎉</title>

        <div className="showcase-header">
          <h1 className="showcase-title">Community Showcase</h1>
          <p className="showcase-subtitle">
            See how developers around the world are using React Bits in their projects
          </p>
          <FadeContent blur delay={500}>
            <a
              href="https://docs.google.com/forms/d/e/1FAIpQLSdlzugJovfr5HPon3YAi8YYSSRuackqX8XIXSeeQmSQypNc7w/viewform?usp=dialog"
              target="_blank"
              rel="noreferrer"
              className="landing-button"
            >
              <span>Submit Your Project</span>
              <div className="button-arrow-circle">
                <svg width="16" height="16" viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
                  <path
                    d="M6 12L10 8L6 4"
                    stroke="currentColor"
                    strokeWidth="2"
                    strokeLinecap="round"
                    strokeLinejoin="round"
                  />
                </svg>
              </div>
            </a>
          </FadeContent>
        </div>

        <FadeContent blur duration={1000} threshold={0} className="fade-grid">
          <div className="grid-container">
            {SHOWCASE_ITEMS.map(item => (
              <Box as="a" href={item.url} rel="noreferrer" target="_blank" className="grid-item" key={item.url}>
                <div className="showcase-img-wrapper">
                  <img
                    className="showcase-img"
                    src={item.image}
                    alt={`Showcase website submitted by: ${item.name ? item.name : 'Anonymous'}`}
                  />
                </div>
                <div className="showcase-info">
                  {item.name && <Text className="author">{item.name}</Text>}
                  <Text className="using">Using {item.using}</Text>
                </div>
              </Box>
            ))}
          </div>
        </FadeContent>
      </section>

      <Footer />
    </>
  );
};

export default ShowcasePage;
