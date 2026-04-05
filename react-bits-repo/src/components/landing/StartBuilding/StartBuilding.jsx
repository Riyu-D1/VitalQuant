import { Link } from 'react-router-dom';
import './StartBuilding.css';

const StartBuilding = () => {
  return (
    <section className="start-building-section">
      <div className="start-building-container">
        <div className="start-building-card">
          <h2 className="start-building-title">Start Exploring</h2>
          <p className="start-building-subtitle">Animations, Components, Backgrounds - One Click Away</p>

          <Link to="/get-started/index" className="start-building-button">
            Browse Components
          </Link>
        </div>
      </div>
    </section>
  );
};

export default StartBuilding;
