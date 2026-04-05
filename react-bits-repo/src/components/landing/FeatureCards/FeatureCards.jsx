import './FeatureCards.css';

const FeatureCards = () => {
  return (
    <div className="features-section">
      <div className="features-container">
        <div className="features-header">
          <h2 className="features-title">Built for Shipping</h2>
          <p className="features-subtitle">Your creative edge for building web projects</p>
        </div>

        <div className="bento-grid">
          <div className="feature-card card1">
            <h2>100%</h2>
            <h3>Free &amp; Open Source</h3>
            <p>Loved by devs around the world</p>
          </div>

          <div className="feature-card card2">
            <h2>110+</h2>
            <h3>Creative Components</h3>
            <p>Fully maintained and growing monthly</p>
          </div>

          <div className="feature-card card4">
            <h2>4</h2>
            <h3>Variants</h3>
            <p>Choose your stack</p>
          </div>

          <div className="feature-card card5">
            <h2>3</h2>
            <h3>Tools</h3>
            <p>Boost your workflow</p>
          </div>
        </div>
      </div>
    </div>
  );
};

export default FeatureCards;
