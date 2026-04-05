import { useEffect, useState, useCallback, useRef } from 'react';
import { AnimatePresence, motion } from 'motion/react';
import { LuX, LuRocket } from 'react-icons/lu';
import './AnnouncementModal.css';

const STORAGE_KEY = 'rb-pro-launch-seen';
const SHOW_DELAY = 1500;
const IMAGE_CYCLE_INTERVAL = 5000;
const IMAGES = ['/assets/rbp/components.webp', '/assets/rbp/blocks.webp'];

const DISABLED = true;

const AnnouncementModal = () => {
  const [isVisible, setIsVisible] = useState(false);
  const [isClosing, setIsClosing] = useState(false);
  const [currentImageIndex, setCurrentImageIndex] = useState(0);
  const modalRef = useRef(null);
  const previouslyFocusedElement = useRef(null);

  useEffect(() => {
    const hasSeenModal = localStorage.getItem(STORAGE_KEY);

    if (hasSeenModal || DISABLED) return;

    const timer = setTimeout(() => {
      previouslyFocusedElement.current = document.activeElement;
      setIsVisible(true);
    }, SHOW_DELAY);

    return () => clearTimeout(timer);
  }, []);

  useEffect(() => {
    if (isVisible && modalRef.current) {
      modalRef.current.focus();
    }
  }, [isVisible]);

  useEffect(() => {
    if (!isVisible || isClosing) return;

    const interval = setInterval(() => {
      setCurrentImageIndex(prev => (prev + 1) % IMAGES.length);
    }, IMAGE_CYCLE_INTERVAL);

    return () => clearInterval(interval);
  }, [isVisible, isClosing]);

  const handleDismiss = useCallback(() => {
    setIsClosing(true);
    localStorage.setItem(STORAGE_KEY, 'true');

    setTimeout(() => {
      setIsVisible(false);
      setIsClosing(false);
      if (previouslyFocusedElement.current) {
        previouslyFocusedElement.current.focus();
      }
    }, 200);
  }, []);

  useEffect(() => {
    const handleKeyDown = e => {
      if (e.key === 'Escape' && isVisible && !isClosing) {
        handleDismiss();
      }

      if (e.key === 'Tab' && isVisible && modalRef.current) {
        const focusableElements = modalRef.current.querySelectorAll(
          'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])'
        );
        const firstElement = focusableElements[0];
        const lastElement = focusableElements[focusableElements.length - 1];

        if (e.shiftKey && document.activeElement === firstElement) {
          e.preventDefault();
          lastElement?.focus();
        } else if (!e.shiftKey && document.activeElement === lastElement) {
          e.preventDefault();
          firstElement?.focus();
        }
      }
    };

    document.addEventListener('keydown', handleKeyDown);
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [isVisible, isClosing, handleDismiss]);

  const handleBackdropClick = e => {
    if (e.target === e.currentTarget) {
      handleDismiss();
    }
  };

  if (!isVisible) return null;

  return (
    <AnimatePresence>
      {!isClosing && (
        <motion.div
          className="announcement-modal-backdrop"
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          exit={{ opacity: 0 }}
          transition={{ duration: 0.2 }}
          onClick={handleBackdropClick}
          aria-hidden="true"
        >
          <motion.div
            ref={modalRef}
            className="announcement-modal"
            role="dialog"
            aria-modal="true"
            aria-labelledby="announcement-modal-title"
            aria-describedby="announcement-modal-description"
            tabIndex={-1}
            initial={{ opacity: 0, scale: 0.95, y: 20 }}
            animate={{ opacity: 1, scale: 1, y: 0 }}
            exit={{ opacity: 0, scale: 0.95, y: 20 }}
            transition={{ duration: 0.25, ease: [0.16, 1, 0.3, 1] }}
            onClick={e => e.stopPropagation()}
          >
            <button className="announcement-modal-close" onClick={handleDismiss} aria-label="Close announcement">
              <LuX size={20} />
            </button>

            <div className="announcement-modal-image">
              <AnimatePresence mode="wait">
                <motion.img
                  key={currentImageIndex}
                  src={IMAGES[currentImageIndex]}
                  alt="React Bits Pro Preview"
                  loading="lazy"
                  initial={{ opacity: 0 }}
                  animate={{ opacity: 1 }}
                  exit={{ opacity: 0 }}
                  transition={{ duration: 0.4 }}
                />
              </AnimatePresence>
            </div>

            <div className="announcement-modal-content">
              <div className="announcement-modal-icon">
                <LuRocket size={24} />
              </div>

              <h2 id="announcement-modal-title" className="announcement-modal-title">
                React Bits Pro just launched!
              </h2>

              <p id="announcement-modal-description" className="announcement-modal-description">
                85+ unique components, 100+ blocks, and 5 full page templates for building memorable products.
              </p>

              <a
                href="https://pro.reactbits.dev"
                target="_blank"
                rel="noopener noreferrer"
                className="announcement-modal-cta"
              >
                Check It Out
              </a>

              <div className="announcement-modal-highlight">
                <span>🎉</span> Launch special: 25% off (limited time)
              </div>
            </div>
          </motion.div>
        </motion.div>
      )}
    </AnimatePresence>
  );
};

export default AnnouncementModal;
