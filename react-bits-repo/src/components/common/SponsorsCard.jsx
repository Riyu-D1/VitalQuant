import { Box, Text, Flex, Icon } from '@chakra-ui/react';
import { FiExternalLink } from 'react-icons/fi';
import { colors } from '../../constants/colors';

import SponsorsCircle from './SponsorsCircle';

const SponsorsCard = () => {
  return (
    <Box className="right-card sponsors-card">
      <Box p="1.25em 1.25em 0.75em">
        <Flex align="center" gap={2} mb={2}>
          <Text fontWeight={700} fontSize="16px" color="#fff" letterSpacing="-.5px">
            Our Sponsors
          </Text>
        </Flex>
        <Text fontSize="14px" color={colors.accentMuted} letterSpacing="-.25px" lineHeight="1.5">
          Help us maintain and grow React Bits, keeping it free for devs worldwide.
        </Text>
      </Box>

      <SponsorsCircle />

      <Box px="1.25em" pb="1.25em" pt="0.5em">
        <a href="mailto:contact@davidhaz.com?subject=React%20Bits%20Sponsorship%20Inquiry" className="sponsor-cta-link">
          <button className="sponsor-button">
            <span>Become a Sponsor</span>
            <Icon as={FiExternalLink} boxSize={3.5} />
          </button>
        </a>
        <Text fontSize="12px" color={colors.accentMuted} textAlign="center" mt={2} letterSpacing="-.25px">
          Get your brand in front of 500k+ devs monthly
        </Text>
        <Text fontSize="10px" color={colors.accentMuted} textAlign="center" mt={2} letterSpacing="-.25px">
          Limited Spots. Secure yours.
        </Text>
      </Box>
    </Box>
  );
};

export default SponsorsCard;
