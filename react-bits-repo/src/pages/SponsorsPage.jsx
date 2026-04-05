import { Box, Text, Flex, Icon } from '@chakra-ui/react';
import { Eye, Sparkles, Star, Component } from 'lucide-react';
import { useEffect } from 'react';
import Header from '../components/navs/Header';
import AnnouncementBar from '../components/common/AnnouncementBar/AnnouncementBar';
import { PRO_ANNOUNCEMENT } from '../constants/Site';
import { colors } from '../constants/colors';
import Sponsors from '../components/landing/Sponsors/Sponsors';
import Footer from '../components/landing/Footer/Footer';
import Aurora from '../content/Backgrounds/Aurora/Aurora';

const StatCard = ({ icon, value, label }) => (
  <Flex
    direction="column"
    align="center"
    gap={2}
    p={6}
    bg="rgba(255,255,255,0.03)"
    border="1px solid rgba(255,255,255,0.08)"
    borderRadius="16px"
    flex={1}
    minW="200px"
    transition="all 0.3s ease"
    _hover={{
      bg: 'rgba(255,255,255,0.05)',
      borderColor: 'rgba(132, 0, 255, 0.3)',
      transform: 'translateY(-2px)'
    }}
  >
    <Icon as={icon} boxSize={6} color="#8400ff" />
    <Text fontSize="2.5rem" fontWeight={700} color="white" letterSpacing="-1px">
      {value}
    </Text>
    <Text fontSize="0.9rem" color="rgba(255,255,255,0.6)" textAlign="center">
      {label}
    </Text>
  </Flex>
);

const SponsorsPage = () => {
  useEffect(() => {
    document.title = 'React Bits - Sponsors';
  }, []);

  return (
    <Box minH="100vh" bg={colors.bgBody} display="flex" flexDirection="column" position="relative" overflow="hidden">
      {/* Animated Background */}
      <Box
        position="fixed"
        top={0}
        left={0}
        right={0}
        bottom={0}
        zIndex={0}
        opacity={0.5}
        transform={'rotate(180deg)'}
        pointerEvents="none"
      >
        <Aurora colorStops={['#3A0CA3', '#7209B7', '#4C1D95']} amplitude={0.8} blend={0.5} />
      </Box>

      {/* Gradient Orbs */}
      <Box
        position="fixed"
        top="-200px"
        right="-200px"
        width="600px"
        height="600px"
        borderRadius="50%"
        background="radial-gradient(circle, rgba(132, 0, 255, 0.15) 0%, transparent 70%)"
        pointerEvents="none"
        zIndex={0}
      />
      <Box
        position="fixed"
        bottom="-300px"
        left="-200px"
        width="800px"
        height="800px"
        borderRadius="50%"
        background="radial-gradient(circle, rgba(82, 39, 255, 0.1) 0%, transparent 70%)"
        pointerEvents="none"
        zIndex={0}
      />

      <AnnouncementBar {...PRO_ANNOUNCEMENT} />
      <Header />

      <Box pt={{ base: '140px', md: '240px' }} flex={1} position="relative" zIndex={1}>
        {/* Hero Section */}
        <Box textAlign="center" mb={12} px={4}>
          <Flex justify="center" mb={4}>
            <Flex
              align="center"
              gap={2}
              bg="rgba(132, 0, 255, 0.1)"
              border="1px solid rgba(132, 0, 255, 0.3)"
              px={4}
              py={2}
              borderRadius="full"
            >
              <Icon as={Sparkles} boxSize={4} color={colors.accent} />
              <Text fontSize="sm" color={colors.accent} fontWeight={500}>
                Support Open Source
              </Text>
            </Flex>
          </Flex>
          <Text
            fontSize={{ base: '2.5rem', md: '3.5rem' }}
            fontWeight={700}
            color="white"
            letterSpacing="-2px"
            lineHeight={'100%'}
            mb={4}
            mx="auto"
            maxW="700px"
          >
            Power the fastest growing creative UI library
          </Text>
          <Text
            fontSize={{ base: '1rem', md: '1.2rem' }}
            color="rgba(255,255,255,0.6)"
            maxW="600px"
            mx="auto"
            lineHeight={1.6}
          >
            Your sponsorship helps us maintain and grow React Bits, keeping it free and open-source for developers
            worldwide.
          </Text>
        </Box>

        {/* Stats Section */}
        <Flex justify="center" gap={4} px={4} mb={16} flexWrap="wrap" maxW="900px" mx="auto">
          <StatCard icon={Eye} value="500K+" label="Monthly Visitors" />
          <StatCard icon={Star} value="35K+" label="GitHub Stars" />
          <StatCard icon={Component} value="110+" label="Components" />
        </Flex>

        <Sponsors />
      </Box>
      <Footer />
    </Box>
  );
};

export default SponsorsPage;
