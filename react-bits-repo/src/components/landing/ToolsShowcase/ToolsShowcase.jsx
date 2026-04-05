import { useRef, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { ArrowRight } from 'lucide-react';
import { TOOLS } from '../../../constants/Tools';
import './ToolsShowcase.css';

const ToolCard = ({ tool, index }) => {
  const cardRef = useRef(null);
  const [isVisible, setIsVisible] = useState(false);

  useEffect(() => {
    const observer = new IntersectionObserver(
      ([entry]) => {
        if (entry.isIntersecting) {
          setTimeout(() => setIsVisible(true), index * 100);
        }
      },
      { threshold: 0.2 }
    );

    if (cardRef.current) {
      observer.observe(cardRef.current);
    }

    return () => observer.disconnect();
  }, [index]);

  const IconComponent = tool.icon;

  return (
    <Link to={`/tools/${tool.id}`} ref={cardRef} className={`tool-showcase-card ${isVisible ? 'visible' : ''}`}>
      <div className="tool-card-content">
        <div className="tool-card-icon">
          <IconComponent size={20} color="#8400ff" />
        </div>

        <h3 className="tool-card-title">{tool.label}</h3>
        <p className="tool-card-description">{tool.description}</p>

        <div className="tool-card-cta">
          <span>Try it out</span>
          <ArrowRight size={14} />
        </div>
      </div>
    </Link>
  );
};

const ToolsShowcase = () => {
  const sectionRef = useRef(null);
  const [titleVisible, setTitleVisible] = useState(false);

  useEffect(() => {
    const observer = new IntersectionObserver(
      ([entry]) => {
        if (entry.isIntersecting) {
          setTitleVisible(true);
        }
      },
      { threshold: 0.3 }
    );

    if (sectionRef.current) {
      observer.observe(sectionRef.current);
    }

    return () => observer.disconnect();
  }, []);

  return (
    <section className="tools-showcase-section" ref={sectionRef}>
      <div className="tools-showcase-container">
        <div className={`tools-showcase-header ${titleVisible ? 'visible' : ''}`}>
          <h2 className="tools-showcase-title">Tools</h2>
          <p className="tools-showcase-subtitle">Free utilities to boost your workflow</p>
        </div>

        <div className="tools-showcase-grid">
          {TOOLS.map((tool, index) => (
            <ToolCard key={tool.id} tool={tool} index={index} />
          ))}
        </div>
      </div>
    </section>
  );
};

export default ToolsShowcase;
