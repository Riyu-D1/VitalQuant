import { Route, BrowserRouter as Router, Routes, useLocation } from 'react-router-dom';
import { NuqsAdapter } from 'nuqs/adapters/react-router/v6';
import Providers from './components/layout/Providers';
import { useEffect } from 'react';
import { ActiveRouteProvider } from './components/context/ActiveRouteContext/ActiveRouteContext';
import { forceChakraDarkTheme } from './utils/utils';

import AnnouncementBar from './components/common/AnnouncementBar/AnnouncementBar';
import { PRO_ANNOUNCEMENT } from './constants/Site';
import AnnouncementModal from './components/common/AnnouncementModal/AnnouncementModal';
import DisplayHeader from './components/landing/DisplayHeader/DisplayHeader';
import SidebarLayout from './components/layout/SidebarLayout';
import LandingPage from './pages/LandingPage';
import CategoryPage from './pages/CategoryPage';
import ShowcasePage from './pages/ShowcasePage';
import FavoritesPage from './pages/FavoritesPage';
import SponsorsPage from './pages/SponsorsPage';
import ToolsPage from './pages/ToolsPage';

function AppContent() {
  const location = useLocation();

  const getActiveItem = () => {
    if (location.pathname === '/') return 'home';
    if (location.pathname === '/showcase') return 'showcase';
    return null;
  };

  const sidebarPages = ['/favorites'];
  const isSidebarPage =
    sidebarPages.some(path => location.pathname.includes(path)) || location.pathname.match(/^\/[^/]+\/[^/]+$/);

  const isToolsPage = location.pathname.startsWith('/tools');
  const isSponsorsPage = location.pathname === '/sponsors';

  return (
    <>
      {!isSidebarPage && !isToolsPage && !isSponsorsPage && (
        <>
          <AnnouncementBar
            {...PRO_ANNOUNCEMENT}
            backgroundColor={location.pathname === '/' ? undefined : '#5227FF'}
            noBorder={location.pathname !== '/'}
            className="landing-bar"
          />
          <DisplayHeader activeItem={getActiveItem()} />
        </>
      )}
      <Providers>
        <Routes>
          <Route exact path="/" element={<LandingPage />} />
          <Route exact path="/showcase" element={<ShowcasePage />} />
          <Route exact path="/sponsors" element={<SponsorsPage />} />
          <Route path="/tools/:toolId?" element={<ToolsPage />} />
          <Route
            path="/:category/:subcategory"
            element={
              <SidebarLayout>
                <CategoryPage />
              </SidebarLayout>
            }
          />

          <Route
            path="/favorites"
            element={
              <SidebarLayout>
                <FavoritesPage />
              </SidebarLayout>
            }
          />
        </Routes>
      </Providers>
    </>
  );
}

export default function App() {
  useEffect(() => {
    forceChakraDarkTheme();
  }, []);

  return (
    <Router>
      <NuqsAdapter>
        <ActiveRouteProvider>
          <AppContent />
          <AnnouncementModal />
        </ActiveRouteProvider>
      </NuqsAdapter>
    </Router>
  );
}
