import { Box } from '@chakra-ui/react';
import Announcement from '../../components/common/Announcement';
import AnnouncementBar from '../common/AnnouncementBar/AnnouncementBar';
import { PRO_ANNOUNCEMENT } from '../../constants/Site';
import Header from '../../components/navs/Header';
import Sidebar from '../../components/navs/Sidebar';
import ProCard from '../common/ProCard';
import SponsorsCard from '../common/SponsorsCard';

export default function SidebarLayout({ children }) {
  return (
    <main className="app-container">
      <AnnouncementBar {...PRO_ANNOUNCEMENT} />
      <Announcement />
      <Header />
      <section className="category-wrapper">
        <Sidebar />

        {children}

        <aside className="right-panel">
          <Box className="right-panel-inner">
            <ProCard />
            <SponsorsCard />
          </Box>
        </aside>
      </section>
    </main>
  );
}
