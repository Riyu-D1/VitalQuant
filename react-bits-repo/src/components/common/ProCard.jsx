import { Box, Text, Flex, Icon } from '@chakra-ui/react';
import { LuArrowRight, LuRocket } from 'react-icons/lu';
import { colors } from '../../constants/colors';

const ProCard = () => {
  return (
    <a href="https://pro.reactbits.dev" target="_blank" rel="noopener noreferrer" className="pro-card-link">
      <Box className="right-card pro-card">
        <Flex direction="column" gap={3} p="1.25em">
          <Flex align="center" gap={2}>
            <Text fontWeight={700} fontSize="20px" color="#fff" letterSpacing="-.5px" lineHeight={0}>
              <Icon opacity={0.35} as={LuRocket} mr="8px" boxSize={6} color={colors.accentMuted} />
              Get React Bits Pro
            </Text>
          </Flex>

          <Text fontSize="12px" color={colors.accentMuted} letterSpacing="-.25px" lineHeight="1.5">
            85+ components, 100+ blocks & 5 templates to ship memorable products faster.
          </Text>

          <Flex align="center" justify="center" className="pro-card-cta" gap={2}>
            <Text fontSize="13px" fontWeight={600} color="#fff">
              Explore Pro
            </Text>
            <Icon as={LuArrowRight} boxSize={4} color="#fff" />
          </Flex>
        </Flex>
      </Box>
    </a>
  );
};

export default ProCard;
